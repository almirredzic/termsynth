# termsynth
ALSA based synthesizer for Linux terminal

## How to try it (only basic usage explained)

* Download it
* Run `make termsynth`
* Run `./termsynth hw:0`
* Connect the (virtual) MIDI keyboard by typing in termsynth command prompt: `exec aconnect 'Your Keyboard Name' 'termsynth'`
* Press tab to read a patch
* Navigate in patch bank using arrow keys (`UP`/`DOWN` - change bank, `LEFT`/`RIGHT` - change patch)
* Press `ENTER` to read a selected patch
* Play
* Type `quit` to exit

## Defining patches

Patch definitions are simple human-readable text files. Each line in a patch definition file represents one module. Patch definition file should end with a new line.

Modules are defined in the following form:

`mmm [name=module_name] input:=modulename_or_absolute_or_relative_position... parameter=value...`

Each module can have multiple inputs (depending on the type of the module) and only one output. The output from the last module in a patch represents that patch output. Output of a patch is a mono signal. To produce stereo output, module `out` should be used as the last module in the patch.

### Example 1

```
osc type=sine detune=0.5
env amp:=-1 t1=0.001 t2=1.0 l3=0.5 t4=0.125
```

This is a simple patch, consisted of one oscillator and one envelope.
Oscillator produces a sine signal at the frequency that is one octave below the frequency of the MIDI note.
Envelope module generates a simple ADSR shape with the attack time of 0.001 seconds, decay time of 1 second, sustain level of 0.5 and the release time of 0.125 seconds.
Envelope module takes input from the previous module by providing the relative position of the source module.
The signal that arrives to the ´amp:´ input is multiplied by the ADSR signal and returned as the envelope (and patch) output.
The envelope module could also reference the oscillator module by its absolute position, which is 1.
If a module reference begins with a '+' or a '-' character, it is treated as a relative module position.
If a module reference begins with a digit ('0' ... '9'), it is treated as an absolute module position.
If a module reference begins with any other character, it is treated as a module name.
To take input from its own output, a module should have the input defined as `some_input:=+0` or `some_input:=-0`.
