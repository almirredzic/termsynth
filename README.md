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


##Module osc (oscillator)

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
It is usually used together with the detune parameter to specify fixed oscillator frequency, regardless of the MIDI note that triggered the voice, e.g. for LFO oscillators or drum sounds:
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

##Module env (envelope)

####amp:
***Meaning:*** Amplitude modulation input

***Default value:*** -1 (no input signal)

***Description:*** Envelope output will be multiplied by the input signal.

---

####shape
***Meaning:*** Shape of the envelope segments

***Default value:*** natural

***Possible values:*** natural, linear

***Description:*** This parameter sets the shape of envelope segments. A linear shaped envelope consists of straight lines.
A natural shaped envelope consist of curved lines. This kind of envelope shape is more common in natural acoustic sounds.
In rising segments, natural shaped envelope first rises fast and then slow to the target level.
In falling segments, it drops fast and then slow to the target level.

***Note:*** In termsynth, for turning linear into natural envelope segment shapes, a square root function is used.

---

####t0
***Meaning:*** Delay time

***Default value:*** 0.0 (Expressed in seconds)

***Description:*** Time before the envelope's attack phase starts. During this time, envelope module will output value l0.

---

####l0
***Meaning:*** Start level

***Default value:*** Value of l4 (End level with default value 0.0)

***Description:*** Start level of the envelope's attack phase.


---

####t1
***Meaning:*** Attack time

***Default value:*** 0.01 (Expressed in seconds)

***Description:*** Envelope's attack phase duration.

---

####l1
***Meaning:*** Attack level

***Default value:*** 1.0

***Description:*** Level reached at the end of the attack phase.

---

####t2
***Meaning:*** Decay time

***Default value:*** 0.0001 (Expressed in seconds)

***Description:*** Envelope's decay phase duration.

---

####l2
***Meaning:*** Breakpoint level

***Default value:*** Value of l3 (Sustain level with default value 1.0)

***Description:*** Level reached at the end of the decay phase.

---

####t3
***Meaning:*** Second decay time

***Default value:*** 0.0001 (Expressed in seconds)

***Description:*** Envelope's second decay phase duration.

---

####l3
***Meaning:*** Sustain level

***Default value:*** 1.0

***Description:*** In the sustain phase envelope module will output a constant number set by this parameter.
Envelope will stay in this phase as long as the MIDI note that triggered the voice is in ON state (as long as the key is pressed).

---

####t4
***Meaning:*** Release time

***Default value:*** 0.01 (Expressed in seconds)

***Description:*** Envelope's release phase duration.

---

####l4
***Meaning:*** End level

***Default value:*** 0.0

***Description:*** Level reached at the end of the release phase.

***Note:*** Release phase is the phase activated when "the key is released" and does not have to start after the sustain phase.
If the key is released sometimes during the attack, decay or second decay phase, envelope will immediately go to the release phase.

***Note:*** Attack segment does not have to be rising. Also the decay and second decay segments do not have to be falling.
Since it is possible to set the start and end level for each phase, then, for example, l0 can have a greater value than l1.
Also the l3 can be set to a greater value than l2, so envelope can rise to the sustain value after the decay phase finish (envelope
shape commonly used for brass sounds).

***Note:*** To get a "classical" ADSR envelope, only these parameters need to be used: t1, t2, l3, t4 (attack time, decay time, sustain level, release time).

##Module flt (filter)

####input:
***Meaning:*** Input signal

***Default value:*** 0 (zero module - a constant zero signal)

---

####cutoff:
***Meaning:*** Cutoff frequency modulation input

***Default value:*** -1 (no input signal)

***Description:*** Cutoff input signal value will be added to the filter's cutoff frequency.
The final cutoff frequency should be in 0.0 to 1.0 range (0 Hz to 22050 Hz).

---

####resonance:
***Meaning:*** Resonance modulation input

***Default value:*** -1 (no input signal)

***Description:*** Resonance input signal value will be added to the filter's resonance.
The final resonance should be in 0.0 to 1.0 range.

---

####cutoff
***Meaning:*** Cutoff frequency

***Default value:*** 1.0 (equals 22050 Hz)

***Description:*** Filter's cutoff frequency normalized to 0.0 to 1.0 range.
The value of this parameter can be also expressed in hertz (Hz):
```
cutoff=0.5
cutoff=hz:11025
```

---

####resonance
***Meaning:*** Resonance

***Default value:*** 0.0

***Description:*** Filter's resonance amount.

---

####type
***Meaning:*** Filter type

***Default value:*** lowpass

***Possible values:*** lowpass, highpass, bandpass, bandstop

***Description:*** This parameter sets the type of the filter.

***Note:*** The filter module is a 12 dB/octave state variable filter.

##Module mix (mixer)

####a: b: c: d: e: f:
***Meaning:*** Six inputs

***Default value:*** 0 (zero module - a constant zero signal)

***Description:*** Input signals that will be summed and returned as the mixer output.

---

####crossfade:
***Meaning:*** Crossfade input signal

***Default value:*** -1 (no input signal)

***Description:*** If this input is not connected, output from the mixer will be a simple sum of its inputs.
* If the crossfade input signal is in range 0.0 to 1.0, output will be a combination of signals from a: and b: inputs.
* If the crossfade input signal is in range 1.0 to 2.0, output will be a combination of signals from b: and c: inputs.
* If the crossfade input signal is in range 2.0 to 3.0, output will be a combination of signals from c: and d: inputs.
* If the crossfade input signal is in range 3.0 to 4.0, output will be a combination of signals from d: and e: inputs.
* If the crossfade input signal is in range 4.0 to 5.0, output will be a combination of signals from e: and f: inputs.

***Note:*** By using an envelope signal as a crossfade input signal and oscillators with the same frequency and different waveforms as inputs, a wavetable-like synthesis method can be implemented.

##Module mul (multiplier)

####a: b:
***Meaning:*** Two inputs

***Default value:*** 0 (zero module - a constant zero signal)

***Description:*** Input signals whose product will be returned as the multiplier output.

***Note:*** This kind of module is also known as ring modulator.

##Module mod (modifier)

####input:
***Meaning:*** Input to the modifier module

***Default value:*** -1 (no input signal)

***Description:*** Input signal that will be modified by the module. If the input is not connected, the value determined with the 'source' parameter will be used as the input value.

---

####source
***Meaning:*** Source of the input value

***Default value:*** frequency (scaled to 0.0 - 1.0 from 0 Hz - 22050 Hz)

***Possible values:*** frequency, velocity, controller, 1/frequency, pitchwheel, midinote

***Description:*** Source of the input value that will be modified by the module.
The following sources are available:
* frequency - MIDI note frequency, normalized from (0 Hz, 22050) to (0.0, 1.0) range
* velocity - MIDI note velocity, normalized from (0, 127) to (0.0, 1.0) range
* controller - MIDI controller value, normalized from (0, 127) to (0.0, 1.0) range. The number of the controller is determined by the 'number' parameter.
* 1/frequency - MIDI note frequency, normalized from (0 Hz, 22050) to (0.0, 1.0) range, then inverted
* pitchwheel - MIDI pitch wheel value normalized to (-1.0, 1.0) range
* midinote - MIDI note value (0, 127)

***Note:*** The 'source' parameter and the 'input:' are mutually exclusive. if both are specified, value from the 'input:' will be used.

---

####number
***Meaning:*** Number of the MIDI controller

***Default value:*** 1 (Modulation wheel)

***Possible values:*** 1 - 127

***Description:*** If the 'controller' is set as the 'source' parameter value, this parameter will select the MIDI controller from which the MIDI CC data will be read.

---

####amp
***Meaning:*** Amplification factor for the source or input value

***Default value:*** 1.0

***Description:*** Value obtained from the input or from a specified source will be multiplied by this parameter's value.

---

####offset
***Meaning:*** Offset for the amplified source or input value

***Default value:*** 1.0

***Description:*** Value obtained from the input or from a specified source and multiplied by the value of the 'amp' parameter will be increased/decreased by the value of this parameter.

---

####scale
***Meaning:*** A function that will scale the amplified and offsetted input/source value

***Default value:*** none

***Possible values:*** none, exp2, inv, int, abs

***Description:*** The functioning of the modifier module can be explained by this expression:
```
module_output = scale_function ( input_or_source_value * amp_value + offset_value )
```
The following scaling functions are available:
* none - f(x) = x, no scaling function is used, only amplification and offset
* exp2 - f(x) = 2 ^ x
* inv - f(x) = 1 / x
* int - f(x) = int(x), taking only the integer part of the value
* abs - f(x) = |x|, absolute value

##Module pol (polynomial function)

####input:
***Meaning:*** Input to the polynomial module

***Default value:*** 0 (zero module - a constant zero signal)

***Description:*** Input signal that will be modified by the module.

---

####a7 a6 a5 a4 a3 a2 a1 a0
***Meaning:*** Polynomial coefficients

***Default value:*** 1.0 for a1, 0.0 for others (default function is f(x) = x)

***Description:*** The polynomial function of this module has the follwing form:
```
f(x) = a7 * x^7 + a6 * x^6 + a5 * x^5 + a4 * x^4 + a3 * x^3 + a2 * x^2 + a1 * x + a0
```
