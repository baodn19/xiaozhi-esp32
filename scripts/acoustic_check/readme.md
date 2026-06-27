# Acoustic Check

This GUI receives PCM audio streamed over UDP from a Xiaozhi device, plots time-domain and frequency-domain views, and can save the captured audio window. Use it to inspect noise frequency distribution and to verify acoustic ASCII transmission accuracy.

Firmware setup: enable `USE_AUDIO_DEBUGGER` and set `AUDIO_DEBUG_UDP_SERVER` to this machine's IP address.

For acoustic demod testing, use `sonic_wifi_config.html` or the PinMe-hosted [Xiaozhi acoustic Wi-Fi provisioning](https://iqf7jnhi.pinit.eth.limo) page to generate test tones.

# Acoustic decode test log

> `✓` = decodes successfully from raw I2S DIN PCM. `△` = stable only with noise reduction or extra steps. `X` = poor even after noise reduction (may decode occasionally but is very unstable).
> Some ADCs need finer noise tuning during I2C setup; results below were tested only with board configs in this repo.

| Device | ADC | MIC | Result | Notes |
| ---- | ---- | --- | --- | --- |
| bread-compact | INMP441 | onboard MEMS mic | ✓ |
| atk-dnesp32s3-box | ES8311 | | ✓ |
| magiclick-2p5 | ES8311 | | ✓ |
| lichuang-dev  | ES7210 | | △ | disable INPUT_REFERENCE during testing |
| kevin-box-2 | ES7210 | | △ | disable INPUT_REFERENCE during testing |
| m5stack-core-s3 | ES7210 | | △ | disable INPUT_REFERENCE during testing |
| xmini-c3 | ES8311 | | △ | noise reduction required |
| atoms3r-echo-base | ES8311 | | △ | noise reduction required |
| atk-dnesp32s3-box0 | ES8311 | | X | receives and decodes, but high packet loss |
| movecall-moji-esp32s3 | ES8311 | | X | receives and decodes, but high packet loss |
