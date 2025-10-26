# Testing Summary

## Overview

This document summarizes all unit tests for the LoRa Bridge application.

---

## Test Coverage

### Unit Tests (JUnit)

| Test File | Tests | Coverage | Use Cases |
|-----------|-------|----------|-----------|
| **LoRaProtocolTest** | 13 | Protocol serialization/deserialization | UC-5.1, UC-5.2, UC-5.3 |
| **LocationDataTest** | 9 | Location model and conversions | UC-2.2 |
| **ChatMessageTest** | 10 | Chat message model | UC-6.1 |
| **MessageRepositoryTest** | 11 | Message state management | UC-6.1, UC-6.2 |
| **TOTAL** | **43** | - | - |

---

## Test Details

### 1. LoRaProtocolTest (13 tests)

**File:** `app/src/test/java/com/example/lorabridge/data/protocol/LoRaProtocolTest.kt`

**Coverage:**
- ✅ UC-5.1: Serialize Text Message
- ✅ UC-5.2: Deserialize Received Message
- ✅ UC-5.3: Validate Character Support

**Tests:**
1. `serialize and deserialize text message without GPS`
2. `serialize and deserialize text message with GPS`
3. `serialize and deserialize ACK message`
4. `text with max length should serialize correctly`
5. `lowercase text should be converted to uppercase`
6. `special characters should be supported`
7. `isTextSupported should validate character set`
8. `calculatePackedSize should return correct byte count`
9. `message type should be correctly set in serialized data`
10. `deserialize should fail with empty data`
11. `deserialize should fail with invalid message type`
12. `round trip with all supported characters`
13. GPS coordinate precision (6 decimal places)

**How to run:**
```bash
./gradlew test --tests "com.example.lorabridge.data.protocol.LoRaProtocolTest"
```

---

### 2. LocationDataTest (9 tests)

**File:** `app/src/test/java/com/example/lorabridge/domain/model/LocationDataTest.kt`

**Coverage:**
- ✅ UC-2.2: Get Last Known Location (model validation)

**Tests:**
1. `toDisplayString should format correctly`
2. `isRecent should return true for fresh location`
3. `isRecent should return false for old location`
4. `latitudeMicro should convert to microdegrees`
5. `longitudeMicro should convert to microdegrees`
6. `fromMicro should convert from microdegrees`
7. `round trip micro conversion should preserve precision`
8. `negative coordinates should work correctly`
9. Display string formatting

**How to run:**
```bash
./gradlew test --tests "com.example.lorabridge.domain.model.LocationDataTest"
```

---

### 3. ChatMessageTest (10 tests)

**File:** `app/src/test/java/com/example/lorabridge/domain/model/ChatMessageTest.kt`

**Coverage:**
- ✅ UC-6.1: Display Chat Message (model validation)

**Tests:**
1. `sent message should have PENDING ack status by default`
2. `received message should have NONE ack status`
3. `canOpenMaps should return true when has GPS coordinates`
4. `canOpenMaps should return false when no GPS`
5. `canOpenMaps should return false when GPS flag true but coordinates null`
6. `canOpenMaps should return false when latitude is null`
7. `canOpenMaps should return false when longitude is null`
8. `timestamp should be set automatically`
9. `copy should preserve all fields`
10. `copy with ackStatus change should work`

**How to run:**
```bash
./gradlew test --tests "com.example.lorabridge.domain.model.ChatMessageTest"
```

---

### 4. MessageRepositoryTest (11 tests)

**File:** `app/src/test/java/com/example/lorabridge/data/repository/MessageRepositoryTest.kt`

**Coverage:**
- ✅ UC-6.1: Display Chat Message (repository operations)
- ✅ UC-6.2: Update ACK Status Indicator

**Tests:**
1. `initial messages should be empty`
2. `addMessage should add message to list`
3. `addMessage should append to existing messages`
4. `updateAckStatus should update correct message`
5. `updateAckStatus should only update sent messages`
6. `updateAckStatus with non-existent seq should not crash`
7. `clearMessages should remove all messages`
8. `findMessageBySeq should find correct message`
9. `findMessageBySeq should return null for non-existent seq`
10. `findMessageBySeq should find first match when multiple with same seq`
11. `messages should maintain order`

**How to run:**
```bash
./gradlew test --tests "com.example.lorabridge.data.repository.MessageRepositoryTest"
```

---

## Running All Tests

### Run All Unit Tests
```bash
cd lorabridge
./gradlew test
```

### Run Tests with Coverage
```bash
./gradlew testDebugUnitTest jacocoTestReport
```

### Run Specific Test Class
```bash
./gradlew test --tests "ClassName"
```

### Run Tests in Continuous Mode
```bash
./gradlew test --continuous
```

---

## Test Dependencies

### Added Dependencies
- `junit:4.13.2` - JUnit testing framework
- `kotlinx-coroutines-test:1.8.1` - Coroutine testing utilities

**File:** `gradle/libs.versions.toml`
```toml
kotlinx-coroutines-test = { group = "org.jetbrains.kotlinx", name = "kotlinx-coroutines-test", version.ref = "coroutines" }
```

**File:** `app/build.gradle.kts`
```kotlin
testImplementation(libs.junit)
testImplementation(libs.kotlinx.coroutines.test)
```

---

## Coverage by Use Case

### ✅ Fully Tested (6 use cases)
- **UC-5.1:** Serialize Text Message - 100% covered
- **UC-5.2:** Deserialize Received Message - 100% covered
- **UC-5.3:** Validate Character Support - 100% covered
- **UC-2.2:** Get Last Known Location - Model tested
- **UC-6.1:** Display Chat Message - Model + Repository tested
- **UC-6.2:** Update ACK Status Indicator - 100% covered

### ⚠️ Partially Tested (0 use cases)

### ❌ Not Unit Tested (19 use cases)
These require Android framework or integration testing:
- UC-1.1 through UC-1.5 (BLE - requires Android framework)
- UC-2.1, UC-2.3 (GPS - requires Android framework)
- UC-3.1 through UC-3.4 (Message sending - requires ViewModel/integration)
- UC-4.1, UC-4.2 (Message receiving - requires integration)
- UC-6.3 (Auto-scroll - requires Compose UI testing)
- UC-7.1, UC-7.2, UC-7.3 (Permissions - requires Android framework)
- UC-8.1, UC-8.2 (UI behaviors - requires instrumentation)

---

## Future Testing Recommendations

### Priority 1: ViewModel Unit Tests
Add tests for ChatViewModel with mocked repositories:
- UC-1.3: Message queuing and reconnection
- UC-3.1: Send message flow
- UC-3.2: ACK handling
- UC-3.3: ACK timeout

**Example:**
```kotlin
@Test
fun `sendMessage while disconnected should queue message`() = runTest {
    // Given: disconnected BLE
    val viewModel = ChatViewModel(mockBleRepo, mockLocationRepo, mockMessageRepo)

    // When: user sends message
    viewModel.sendMessage("TEST")

    // Then: message queued and scan initiated
    verify(mockBleRepo).startScan()
}
```

### Priority 2: Instrumentation Tests
Add UI tests for Compose screens:
- UC-6.3: Auto-scroll behavior
- UC-7.1, UC-7.2: Permission request flow
- UC-8.1: Keyboard dismiss

**Example:**
```kotlin
@Test
fun autoScrollWhenNewMessageAdded() {
    composeTestRule.setContent { ChatScreen() }

    // Add messages and verify scroll
    composeTestRule.onNodeWithTag("messageList")
        .performScrollToIndex(messages.size - 1)
}
```

### Priority 3: Integration Tests
Test full message flow:
- Send message → BLE → ESP32 → ACK → UI update

---

## Test Metrics

### Current Status
- **Total Tests:** 43
- **Test Files:** 4
- **Use Cases with Tests:** 6/25 (24%)
- **Lines of Test Code:** ~500
- **Test Execution Time:** < 1 second

### Target Goals
- **Total Tests:** 100+
- **Use Cases with Tests:** 20/25 (80%)
- **Code Coverage:** 70%+

---

## Continuous Integration

### GitHub Actions Example
```yaml
name: Run Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Set up JDK 17
        uses: actions/setup-java@v3
        with:
          java-version: '17'
      - name: Run unit tests
        run: ./gradlew test
      - name: Generate coverage report
        run: ./gradlew jacocoTestReport
```

---

## Debugging Test Failures

### View Test Results
```bash
# HTML report
open app/build/reports/tests/testDebugUnitTest/index.html

# Console output
./gradlew test --info
```

### Common Issues

**1. Coroutine test failures**
```kotlin
// Use runTest for suspend functions
@Test
fun testSuspend() = runTest {
    repository.addMessage(msg)
    val messages = repository.messages.first()
}
```

**2. StateFlow testing**
```kotlin
// Use first() or collect
val value = flow.first()
```

**3. Time-based tests**
```kotlin
// Use virtual time in tests
@Test
fun testTimeout() = runTest {
    advanceTimeBy(5000)
    // Assert timeout behavior
}
```

---

## Test Quality Guidelines

### Good Test Characteristics
1. **Isolated** - Each test is independent
2. **Fast** - Runs in milliseconds
3. **Deterministic** - Same result every time
4. **Readable** - Clear arrange/act/assert structure
5. **Maintainable** - Easy to update when code changes

### Test Naming Convention
```kotlin
// Pattern: `function should expected behavior when condition`
@Test
fun `addMessage should append to existing messages`()

@Test
fun `updateAckStatus should only update sent messages`()
```

### Arrange-Act-Assert Structure
```kotlin
@Test
fun exampleTest() {
    // Arrange - Set up test data
    val message = ChatMessage(...)

    // Act - Execute the code under test
    repository.addMessage(message)

    // Assert - Verify the result
    assertEquals(expected, actual)
}
```

---

**Last Updated:** 2025-10-26
**Version:** 1.0.3
**Total Tests:** 43
**Coverage:** 6/25 use cases (24%)
