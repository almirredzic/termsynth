# termsynth
ALSA based synthesizer for Linux terminal

## How to try it (only basic usage explained)

* Download it
* Run `make termsynth`
* Run `./termsynth hw:0`
* Connect the (virtual) MIDI keyboard by typing in termsynth command prompt: `exec aconnect 'Your Keyboard Name' 'termsynth'`
* Press tab to read a patch
* Navigate using arrow keys (`UP`/`DOWN` - change bank, `LEFT`/`RIGHT` - change patch)
* Press `ENTER` to read a selected patch
* Play
* Type `quit` to exit
