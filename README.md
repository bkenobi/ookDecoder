# ookDecode
Arduino decoder for multipls 433MHz wireless sensors

Currently supported sensors:
 * Blueline power meter reader
 * Acurite 5n1 weather station
 * Acurite 00592TX temperature sensor

It is recommended that a superheterodyne radio be used rather than superregenerative due to significant improvements in range.  RF69 based radio support is in the works and should be available in the future.

This code has been based on several projects and is not intended to be represented as fully my own work.  Among others, I have based this project on:
Powermon433:
  https://github.com/CapnBry/Powermon433
  https://github.com/scruss/Powermon433
acurite5n1arduino:
  https://github.com/zerog2k/acurite5n1arduino
Ray Wang's Acurite 592TX code:
  http://rayshobby.net/?p=8998
ookDecode sourced from (but based on JeeLabs):
  https://github.com/Cactusbone/ookDecoder
  http://jeelabs.net/projects/cafe/wiki/Decoding_the_Oregon_Scientific_V2_protocol

