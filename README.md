# termsynth
ALSA based synthesizer for Linux terminal

## Using termsynth

* Download it
* Run `make termsynth`
* Run `./termsynth hw:0`
* Connect the (virtual) MIDI keyboard by typing in termsynth command prompt: `exec aconnect 'Your Keyboard Name' 'termsynth'`
* Press `TAB` to read a patch
* Navigate in patch bank using arrow keys (`UP`/`DOWN` - change bank, `LEFT`/`RIGHT` - change patch)
* Press `ENTER` to read a selected patch
* Play
* Type `quit` to exit

Patch can also be loaded by typing `read patchname` into termsynth command prompt.

### Loading multiple patches

* When started, termsynth display the contents of the patch 0
* Use `LEFT`/`RIGHT` keys to change patch number (patch slot)
* In another patch slot, press `TAB` to load a patch definition from bank
* Patch 0 is set to receive MIDI data from MIDI channel 0 (MIDI channels are numbered 0 - 15)
* All other patch slots have MIDI channel set to -1, which means they will not receive MIDI data by default
* To set the MIDI channel of a patch slot to e.g. 0, run command `midichannel 0`
* Each patch volume is 1.0 by default
* To set the volume of a patch to e.g. 0.5, run `volume 0.5`

### Layering

* Simply set the same MIDI channel for two or more patches
* Set the volume for each patch to get the desired volume ratio

### Keyboard splitting

* Set the same MIDI channel for two or more patches
* Each patch slot receive the full range of MIDI notes by default (0 - 127) and this range can be changed

Example - use three patches accross the keyboard

* Go to the first patch slot and run `startnote 0` and then run `endnote 60`
* Go to the second patch slot and run `startnote 61` and the run `endnote 72`
* Go to the third patch slot and run `startnote 73` and the run `endnote 127`

### Adding effects

Effects are designed just like regular patches, with only one difference: they take input from regular patches or other effect patches (in case effects are chained).

To load a patch with one or more effects added, do the following:

* Press `TAB` once
* Using arrow keys select a regular patch
* Press `TAB` once more
* Again, using arrow keys, select an effect patch (patches stored in the `effects` bank)
* To add more effects to the chain, just press `TAB` once again
* When done press `ENTER` to read all selected patches into the patch slot

Patch with effects added can also be loaded by typing `read patchname effect1name effect2name...` into termsynth command prompt.

Because of the way the effect patches are designed, loading regular patch with one or more effects added is nothing else but reading multiple patch files in sequence. Permanent addition of an effect to a regular patch can be done simply by appending the contents of the effect patch file to the contents of the regular patch file. Or, even simpler, after loading the patch together with the effects, running the command `write somenewpatchwitheffects`.

### Patch randomization

It is possible to define patches that way, that on each patch load, patch modules get different values for their parameters. In module definition, instead the exact parameter value, e.g. `type=saw` for oscillator waveform type, you can write `type=saw|sine|square|triangle` and on each patch load this oscillator waveform would get a random shape (from the set of the four possible values defined).

To get a feeling how this patch randomization can be used, do the following:

* Press `TAB` to activate the patch bank
* Load the `template/randfm` patch
* Play some notes
* Press `TAB` again - patch `template/randfm` should already be selected, so just press `ENTER` to load it
* Play some notes again - notice the difference in patch definition and sound
* To load a new random combination of parameter values, just press `TAB` and `ENTER` again
* When you get some interesting result, save it using `write patchname` command.

To create a new template like `randfm`, simply copy one of the existing patches from the `bank1` to the `template` bank and using a text editor replace a couple of parameter values with a sequence of '|' delimited possible values.

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


##osc module

####phase:
***Meaning:*** Phase modulation input

***Default value:*** -1 (no input signal)

***Description:*** Oscillator's phase will be modulated by the signal from this input. This kind of modulation is used in DX7 and similar synths.

---

####pitch:
***Meaning:*** Pitch modulation input

***Default value:*** -1 (no input signal)

***Description:*** Oscillator's frequency will be modulated by the 2 ^ input_signal_value. If the input value oscillates between -1 and 1,
frequency of the oscillator will be oscillating from 0.5 * f and 2 * f (one octave below and on octave above the original frequency)

---

####amp:
***Meaning:*** Amplitude modulation input

***Default value:*** -1 (no input signal)

***Description:*** Oscillator's output signal will be multiplied by the input signal. This kind of modulation is also known as the ring modulation.
To obtain an amplitude modulation effect, unipolar input signal should be used.

---

####detune
***Meaning:*** Detune factor for for the oscillator's frequency

***Default value:*** 1.0

***Description:*** Oscillator's frequency is by default set to the frequency of the MIDI note that triggered the voice. Oscillator's frequency will be multiplied by the value of this parameter.
The value of this parameter can be expressed as a number or in semitones and/or cents:
```
detune=0
detune=1.001
detune=0.5
detune=10.0
detune=semi:-5
detune=semi:12
detune=cent:-2
detune=cent:2
detune=semi:-1,cent:-23
```

---

####frequency
***Meaning:*** Frequency offset for the oscillator's frequency

***Default value:*** 0.0 (expressed in Hertz)

***Description:*** The value of this parameter is added to the frequency of the oscillator.
It is usually used together with the detune parameter to specify fixed oscillator frequency, regardles of the MIDI note that triggered the voice, e.g. for LFO oscillators or drum sounds:
```
detune=0 frequency=0.5
detune=0 frequency=200
```

---

####type
***Meaning:*** Oscillator waveform type

***Default value:*** sine

***Possible values:*** sine, saw, square, triangle, pulse

***Description:*** This parameter sets the waveform shape of the oscillator.

---

####harmonics
***Meaning:*** Number of harmonics in the oscillator's waveform
***Default value:*** 64

***Description:*** This parameter sets the number of harmonics in case the waveform is set to saw, square, triangle or pulse. If the waveform is set to sine, this parameter has no effect.

***Note:*** Oscillator waveforms in termsynth are generated as harmonic series (by adding sine signals of different frequency and amplitude) and stored into wavetables.
Using an oscillator with a high number of harmonics can cause aliasing if the patch is played in the upper keyboard range.

---

####pulsewidth
***Meaning:*** Pulse width of the pulse waveform

***Default value:*** 50 (Expressed in percent)

***Description:*** If the oscillator's waveform is set to pulse, this parameter will set the pulse width of the waveform. Otherwise, it will be ignored.

***Note:*** There is no pulse width modulation implemented in termsynth. To get a similar effect, try mixing the outputs from two slightly detuned saw oscillators.
