# ESP8266 Networked CW Keyer

### Warning: the code in this repository is ALPHA, and although it is being used on the air successfully, likely has bugs and lots of papercuts, along with some large known limitations. If you try it, please report bugs, or file pull requests with fixes. 

 This project is based on the following projects:
 Morse Code Keyer (C) 2017 Doug Hoyte
 https:hoytech.com/articles/morse-code-keyer
 https:github.com/hoytech/morse-code-keyer
 2-Clause BSD License
 Modified by ea4aoj 23/04/2020
 Modified by ea4hew 15/09/2020
 https:www.eacwspain.es/2021/05/22/morse-code-keyer-esp8266/

## How the project started.

I've been wanting to have remote access to the shack for some time, as in the winter it's a pretty chilly place to be. As I operate mostly CW, it complicates things some. I'm a big fan of the ESP8266 as a cheap network-connected platform, and thought I'd try employing a couple as a cheap alternative to commercial remote hardware.

The K3NG keyer (based on the Arduino) had all the functionality I needed, but not the WIFI without additional hardware. There is an ESP32 networked keyer, but I have a drawerful of ESP8266s, and that keyer adds features (and hardware) I didn't need. So after searching for ESP8266 keyer code, I took the code from EA4AOJ as my starting point. That code featured:

- Three modes: iambic mode A, vibroplex, and straight key.
- Three memories.

From there, the following features have been added:

- Speed set to wpm, rather than by ear.
- Iambic mode B, with element completion.
- IP networking for remote operation.

Under the hood, a bunch of changes and refactoring were necessary to add networking. Paddle processing was broken out, so that it could be used consistently for sending and memory recording. An ASCII lookup table was added to allow playing of chars and strings in Morse for parameter announcements. The switches were moved to the analog input to free up some pins.
An eeprom-rotating library was used to lessen the hit on flash by memory functions.

## Where the project is now.

I've been using it on the air, and the basic functionality I need is there. But this first version has limitations which need to be revisited (see below). And it is very ALPHA, as in there are likely lots of papercuts yet to be discovered.

## Known limitations.

The biggest limitation is the use of buffering to maintain inter-character spacing, so right now there is a 2 character delay before the remote starts sending. Depending on the characters, this can range in the 1-2 second area at 20 WPM. I don't find this to be a problem in day-to-day ragchews, but it would be ugly trying to break a pileup, a contest, or other timing-critical situations.
The remote functionality only works in iambic keyer mode.
The network packets support characters of up to 8 elements, which is fine unless you are sending long strings of dits or dahs. If you do, the code will pause and send a packet for each 8 elements, which will cause a slight pause in the sidetone.
The code is configured to be compiled with PlatformIO under VS Code, and has not been tested with other platforms/IDEs. The networking is hard-wired in, and depends on #defines.

# What's next.

The first thing that will be changed is to allow selection of network function to be done by keypress at boot-up (currently #defined in code).
Iambic mode B is currently the hardcoded selection, and that needs to be changed to allow push-button configuration.
The buffering scheme employed got the remote code working, but I want to revisit that design and see how it could be done in a way that reduces the latency substantially.
Setting the speed is better than it was, but it's still not really handy. I think a rotary encoder might do the trick.
Networking configuration has to be baked in at this point, and I'd like to change that so that config could be done more flexibly.
The whole thing needs a good code review, and that's where you come in...
If you try the code, I'd love to hear how it goes. When you find bugs, please report them, or even better, fix them and file a pull request. 

## Basic Functions:

Set up the network parameters found in the file include/Network.h.
Burn your ESPs.

When the code boots up, it announces the current speed. Then it attempts to connect to the
network. If sucessful, the server (the ESP at the rig) will announce "S", and the client (the ESP at the remote location) will announce "C". Failure to connect announces "NO PORT".

The code defaults to 20 WPM and iambic Mode B on hard reset. The eeprom stores configuration across soft resets.

A quick press of the Setup button, and you enter the speed configuration mode (the keyer starts sending a string of dits). You can change the speed with the paddles, and as you do, the WPM will be announced. You can interrupt that with another key press. To exit, press the Setup button again.

A LONG press of the Setup button, and you enter the tone configuration mode. Change tone with the paddles, and to exit press the Setup button again.

Long press on one of the memories to record that memory. The keyer will count you down, and then start recording your keying. Press the Setup button when finished and it is memorized.

Short press on one of the memories to play that memory.

Press the Setup button and hold it while immediately pressing a memory button to select the keyer mode:

 1: Switch to paddle handler by pressing Memory1.
 2: Switch to staright key by pressing Memory2.
 3: Switch to vibroplex by pressing Memory3.
