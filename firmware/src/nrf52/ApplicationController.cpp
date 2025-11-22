#include "nrf52/ApplicationController.h"

ApplicationController::ApplicationController()
    : bleConnected(false),
      lastActivityTime(0),
      connectionEstablishedTime(0),
      messagesSent(0),
      messagesReceived(0)
{
}

void ApplicationController::setBLEConnected(bool connected)
{
    bleConnected = connected;
    if (connected)
    {
        lastActivityTime = millis();
        connectionEstablishedTime = millis();
    }
    else
    {
        connectionEstablishedTime = 0;
    }
}

bool ApplicationController::isBLEConnected() const
{
    return bleConnected;
}

void ApplicationController::markActivity()
{
    lastActivityTime = millis();
}

unsigned long ApplicationController::getLastActivityTime() const
{
    return lastActivityTime;
}

unsigned long ApplicationController::getTimeSinceLastActivity() const
{
    if (lastActivityTime == 0)
    {
        return 0;
    }
    return millis() - lastActivityTime;
}

unsigned long ApplicationController::getConnectionDuration() const
{
    if (!bleConnected || connectionEstablishedTime == 0)
    {
        return 0;
    }
    return millis() - connectionEstablishedTime;
}

void ApplicationController::incrementMessagesSent()
{
    messagesSent++;
}

void ApplicationController::incrementMessagesReceived()
{
    messagesReceived++;
}

uint32_t ApplicationController::getMessagesSent() const
{
    return messagesSent;
}

uint32_t ApplicationController::getMessagesReceived() const
{
    return messagesReceived;
}
