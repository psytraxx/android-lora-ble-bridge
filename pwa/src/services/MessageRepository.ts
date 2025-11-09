/**
 * Message Repository
 *
 * Manages chat messages with persistent storage
 */

export enum AckStatus {
  NONE = 'NONE',
  PENDING = 'PENDING',
  DELIVERED = 'DELIVERED'
}

export interface ChatMessage {
  id: string;
  text: string;
  isSent: boolean;
  timestamp: number;
  seq: number;
  ackStatus: AckStatus;
  hasGps: boolean;
  latitude?: number;
  longitude?: number;
}

type MessageListener = (messages: ChatMessage[]) => void;

/**
 * Message Repository for storing and managing chat messages
 */
export class MessageRepository {
  private messages: ChatMessage[] = [];
  private listeners = new Set<MessageListener>();
  private nextSeq = 0;
  private storageKey = 'lora-bridge-messages';

  constructor() {
    this.loadFromStorage();
  }

  /**
   * Get all messages
   */
  getMessages(): ChatMessage[] {
    return [...this.messages];
  }

  /**
   * Add a new sent message
   */
  addSentMessage(text: string, hasGps: boolean, latitude?: number, longitude?: number): ChatMessage {
    const message: ChatMessage = {
      id: this.generateId(),
      text,
      isSent: true,
      timestamp: Date.now(),
      seq: this.nextSeq,
      ackStatus: AckStatus.PENDING,
      hasGps,
      latitude,
      longitude
    };

    this.nextSeq = (this.nextSeq + 1) % 256; // 0-255 wrapping
    this.messages.push(message);
    this.notifyListeners();
    this.saveToStorage();

    return message;
  }

  /**
   * Add a received message
   */
  addReceivedMessage(text: string, seq: number, hasGps: boolean, latitude?: number, longitude?: number): ChatMessage {
    const message: ChatMessage = {
      id: this.generateId(),
      text,
      isSent: false,
      timestamp: Date.now(),
      seq,
      ackStatus: AckStatus.NONE,
      hasGps,
      latitude,
      longitude
    };

    this.messages.push(message);
    this.notifyListeners();
    this.saveToStorage();

    return message;
  }

  /**
   * Update ACK status for a message by sequence number
   */
  updateAckStatus(seq: number, status: AckStatus): boolean {
    const index = this.messages.findIndex(m => m.isSent && m.seq === seq);
    if (index !== -1) {
      // Create new message object to trigger reactivity
      this.messages[index] = {
        ...this.messages[index],
        ackStatus: status
      };
      this.notifyListeners();
      this.saveToStorage();
      return true;
    }
    return false;
  }

  /**
   * Find message by sequence number
   */
  findBySeq(seq: number): ChatMessage | undefined {
    return this.messages.find(m => m.seq === seq);
  }

  /**
   * Clear all messages
   */
  clearMessages(): void {
    this.messages = [];
    this.notifyListeners();
    this.saveToStorage();
  }

  /**
   * Get next sequence number
   */
  getNextSeq(): number {
    return this.nextSeq;
  }

  /**
   * Subscribe to message updates
   */
  onMessagesChange(listener: MessageListener): () => void {
    this.listeners.add(listener);
    // Immediately notify with current state
    listener(this.getMessages());
    return () => this.listeners.delete(listener);
  }

  /**
   * Private: Generate unique ID
   */
  private generateId(): string {
    return `${Date.now()}-${Math.random().toString(36).substring(2, 9)}`;
  }

  /**
   * Private: Notify listeners
   */
  private notifyListeners(): void {
    const messages = this.getMessages();
    this.listeners.forEach(listener => listener(messages));
  }

  /**
   * Private: Save to localStorage
   */
  private saveToStorage(): void {
    try {
      const data = {
        messages: this.messages,
        nextSeq: this.nextSeq
      };
      localStorage.setItem(this.storageKey, JSON.stringify(data));
    } catch (error) {
      console.error('Failed to save messages to storage:', error);
    }
  }

  /**
   * Private: Load from localStorage
   */
  private loadFromStorage(): void {
    try {
      const stored = localStorage.getItem(this.storageKey);
      if (stored) {
        const data = JSON.parse(stored);
        this.messages = data.messages || [];
        this.nextSeq = data.nextSeq || 0;
        console.log(`Loaded ${this.messages.length} messages from storage`);
      }
    } catch (error) {
      console.error('Failed to load messages from storage:', error);
      this.messages = [];
      this.nextSeq = 0;
    }
  }
}

// Singleton instance
export const messageRepository = new MessageRepository();
