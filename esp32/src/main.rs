#![no_std]
#![no_main]

use crate::protocol::Message;
use core::sync::atomic::{AtomicU8, Ordering};
use embassy_executor::Spawner;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;
use embassy_time::{Duration, Timer};
use embedded_hal_async::spi::SpiBus;
use esp_alloc::{self as _, heap_allocator};
use esp_hal_embassy::main;
use static_cell::StaticCell;

mod protocol;

extern crate alloc;

static SEQ_COUNTER: AtomicU8 = AtomicU8::new(0);
static BLE_TO_LORA: StaticCell<Channel<CriticalSectionRawMutex, Message, 1>> = StaticCell::new();
static LORA_TO_BLE: StaticCell<Channel<CriticalSectionRawMutex, Message, 1>> = StaticCell::new();

#[main]
async fn main(spawner: Spawner) {
    heap_allocator!(size: 72 * 1024);

    let peripherals = esp_hal::init(esp_hal::Config::default());

    // BLE setup
    let timer = esp_hal::timer::timg::TimerGroup::new(peripherals.TIMG0);
    let ble_controller = BleController::new(timer.timer0, peripherals.BT);
    let ble = ble_controller.init().await;
    let ble_device = ble.device();

    // Init channels
    let ble_to_lora = BLE_TO_LORA.init(Channel::new());
    let lora_to_ble = LORA_TO_BLE.init(Channel::new());

    // Spawn BLE task
    spawner
        .spawn(ble_task(
            ble_device,
            ble_to_lora.sender(),
            lora_to_ble.receiver(),
        ))
        .unwrap();

    // SPI for LoRa
    let spi = peripherals.SPI2;
    let sck = peripherals.GPIO18;
    let miso = peripherals.GPIO19;
    let mosi = peripherals.GPIO23;
    let cs = peripherals.GPIO5;
    let rst = peripherals.GPIO12;
    let dio0 = peripherals.GPIO32;

    // TODO: Initialize SPI and LoRa at 433 MHz

    loop {
        Timer::after(Duration::from_millis(100)).await;
    }
}

#[embassy_executor::task]
async fn ble_task(
    ble_device: BleDevice<'static>,
    mut ble_to_lora: embassy_sync::channel::Sender<'static, CriticalSectionRawMutex, Message, 1>,
    mut lora_to_ble: embassy_sync::channel::Receiver<'static, CriticalSectionRawMutex, Message, 1>,
) {
    let mut server = GattServer::new(ble_device);

    // Create service
    let service = server.create_service(0x1234);

    // Create characteristics
    let tx_char = service.create_characteristic(
        0x5678,
        CharacteristicProps::READ | CharacteristicProps::WRITE | CharacteristicProps::NOTIFY,
    );
    let rx_char = service.create_characteristic(
        0x5679,
        CharacteristicProps::READ | CharacteristicProps::WRITE | CharacteristicProps::NOTIFY,
    );

    // Start advertising
    server.start_advertising("ESP32-LoRa", &[0x1234]).await;

    loop {
        // Handle incoming from LoRa
        if let Ok(msg) = lora_to_ble.try_receive() {
            let mut buf = [0u8; 512];
            if let Ok(len) = msg.serialize(&mut buf) {
                tx_char.notify(&buf[..len]).await;
            }
        }

        // Handle BLE events
        if let Some(event) = server.next_event().await {
            match event {
                GattEvent::Write { handle, data } => {
                    if handle == rx_char.handle {
                        // Process incoming data
                        if let Ok(msg) = Message::deserialize(&data) {
                            // Send to LoRa
                            let _ = ble_to_lora.send(msg).await;
                        }
                    }
                }
                _ => {}
            }
        }
        Timer::after(Duration::from_millis(10)).await;
    }
}

use embedded_hal_bus::spi::ExclusiveDevice;
use esp_hal::delay::Delay;
use lora_phy::{
    iv::GenericSx127xInterfaceVariant,
    mod_params::*,
    sx127x::{self, Sx1276, Sx127x},
    LoRa,
};

pub const LORA_FREQUENCY_IN_HZ: u32 = 433_050_000_u32;

pub struct SharedLoRa<SPI>
where
    SPI: embedded_hal_async::spi::SpiDevice,
{
    lora: LoRa<
        Sx127x<
            SPI,
            GenericSx127xInterfaceVariant<esp_hal::gpio::Output, esp_hal::gpio::Input>,
            Sx1276,
        >,
        Delay,
    >,
    modulation_params: ModulationParams,
    rx_packet_params: lora_phy::RxPacketParams,
}

impl<SPI> SharedLoRa<SPI>
where
    SPI: embedded_hal_async::spi::SpiDevice,
{
    pub async fn new(spi: SPI, reset: esp_hal::gpio::Output, irq: esp_hal::gpio::Input) -> Self {
        let sx127x_config = sx127x::Config {
            chip: Sx1276,
            rx_boost: true,
            tx_boost: false,
            tcxo_used: false,
        };

        let iv = GenericSx127xInterfaceVariant::new(reset, irq, None, None).unwrap();
        let lora = LoRa::new(Sx127x::new(spi, iv, sx127x_config), false, Delay::new())
            .await
            .expect("Failed to initialize LoRa");

        let modulation_params = lora
            .create_modulation_params(
                SpreadingFactor::_7,
                Bandwidth::_125KHz,
                CodingRate::_4_5,
                LORA_FREQUENCY_IN_HZ,
            )
            .expect("Failed to create modulation params");

        let rx_packet_params = lora
            .create_rx_packet_params(8, false, 256, true, false, &modulation_params)
            .expect("Failed to create rx packet params");

        let mut slf = Self {
            lora,
            modulation_params,
            rx_packet_params,
        };

        slf.lora
            .prepare_for_rx(
                lora_phy::RxMode::Continuous,
                &slf.modulation_params,
                &slf.rx_packet_params,
            )
            .await
            .expect("Failed to prepare for RX");

        slf
    }

    pub async fn send_data(&mut self, data: &[u8]) {
        let mut tx_packet_params = self
            .lora
            .create_tx_packet_params(8, false, true, false, &self.modulation_params)
            .unwrap();

        self.lora
            .prepare_for_tx(&self.modulation_params, &mut tx_packet_params, 20, data)
            .await
            .unwrap();

        self.lora.tx().await.unwrap();

        self.lora.sleep(false).await.unwrap();
    }

    pub async fn send_ack(&mut self, seq: u8) {
        let msg = Message::Ack(AckMessage { seq });
        let mut buf = [0u8; 2];
        let len = msg.serialize(&mut buf).unwrap();
        self.send_data(&buf[..len]).await;
    }

    pub async fn receive(&mut self, buffer: &mut [u8]) -> Result<usize, ()> {
        match self.lora.rx(&self.rx_packet_params, buffer).await {
            Ok((len, _)) => Ok(len as usize),
            Err(_) => Err(()),
        }
    }

    pub async fn prepare_for_rx_single(&mut self) {
        self.lora
            .prepare_for_rx(
                lora_phy::RxMode::Single,
                &self.modulation_params,
                &self.rx_packet_params,
            )
            .await
            .unwrap();
    }

    pub async fn prepare_for_rx_continuous(&mut self) {
        self.lora
            .prepare_for_rx(
                lora_phy::RxMode::Continuous,
                &self.modulation_params,
                &self.rx_packet_params,
            )
            .await
            .unwrap();
    }
}
