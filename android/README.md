# margin Android companion

Kotlin + Jetpack Compose app that is the "brain" for the margin reader: it talks to the LLM, keeps
history, and streams pre-cleaned text to the X4 Pro over BLE (see [../docs/BLE_PROTOCOL.md](../docs/BLE_PROTOCOL.md)).

## Status

Scaffold. Nothing here has run on a phone yet.

| Part | State |
|---|---|
| `protocol/` (framing, reassembly, CTRL, TLV, messages) | Real code. JVM unit tests pass and match the firmware's golden vectors. |
| `ble/MarginBleClient` | Untested GATT client sketch with TODOs (op queue, scanning, bonding, timeouts). |
| `llm/` `LlmProvider`, `OpenAiCompatibleProvider`, `OllamaProvider` | Streaming implementations written against the documented APIs, not yet run against a server. |
| `chat/ChatViewModel`, `chat/TextPrep` | Minimal wiring. No persistence, no settings, no tool calls. |
| `ui/ChatScreen` | Placeholder Compose screen. |

The Gradle project has **not been built**: the box this was scaffolded on has no Android SDK, and
the Gradle wrapper JAR/scripts are not committed. Open `android/` in Android Studio (it will offer to
create the wrapper), or run `gradle wrapper --gradle-version 8.11.1` once with a local Gradle.

## Running the protocol tests without Android

The protocol package is plain Kotlin + JUnit, so it can be checked with just `kotlinc`:

```bash
cd android/app/src
kotlinc main/java/dev/margin/protocol/*.kt test/java/dev/margin/protocol/*.kt \
  -cp junit-4.13.2.jar:hamcrest-core-1.3.jar -d /tmp/kout
java -cp /tmp/kout:junit-4.13.2.jar:hamcrest-core-1.3.jar:$KOTLIN_HOME/lib/kotlin-stdlib.jar \
  org.junit.runner.JUnitCore dev.margin.protocol.GoldenVectorsTest dev.margin.protocol.FramingTest
```

With the Android SDK: `./gradlew :app:testDebugUnitTest`.

## Design notes

- API keys stay on the phone (TODO: EncryptedSharedPreferences / Keystore). Nothing secret crosses BLE.
- `minSdk 33` so the newer `BluetoothGatt.writeCharacteristic(ch, bytes, type)` API and the
  Android 12+ permission model (`BLUETOOTH_SCAN` with `neverForLocation`) can be used throughout.
- TODO(M2): use `CompanionDeviceManager` to associate the reader, which allows background
  reconnects without broad scanning.
