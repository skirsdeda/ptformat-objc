# ptformat-objc

ProTools file format parsing library (C++) with additional Objective-C API. Originally, a fork of zamaudio/ptformat with aim to add support for tempo/time signature/key signature tracks and metadata block. However since original code needed some fixing (either due to bugs or too much code duplication), multiple parts have been rewritten.

## Time position encoding

Time position in ProTools session is encoded either as samples since beginning (**Sample time**) or ticks since beginning (**Musical time**).
**Sample time** starts at 0 while **Musical time** starts at 10^12 (one trillion). Since max sample time is less than 10^12, it's usually pretty easy to tell different time bases apart.

ProTools has 24h limit for session length. This implies that the following are **max** values for time positions:
- **Sample time**: 192000 * 3600 * 24 = 16588800000 (max supported sample rate being 192000Hz)
- **Musical time**: 3600 * 24 / (60 / 999) * 960000 + 10^12 = 2381017600000 (max supported BPM being 999; 960000 ticks per beat)

Time position can appear in both fixed length as well as variable length fields (up to 8 bytes). 
In most places time position is just unsigned integer, but there are cases where time offset can be a signed value (negative value is possible).
Since maximum time position value (max musical time value) is less than max int64 (signed), it should be safe to use **int64** for all time positions.
