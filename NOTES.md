# Notes on TTi TG5011 waveforms
The device can store up to 4 arbitrary waveforms in its internal memory of 128k points, or 1000 waveforms on an external USB drive.
## TTi Waveform Manager Plus
TTi provides a waveform manager which should help you create waveforms.
The waveform editor has its own internal waveform format where the amplitude is always set to 32768.
This is then stored in wfm binary files where the first 2 bytes define the amplitude, and the following stream of bytes are the data, where each bin is 2 bytes.

For the TG5011, the waveforms can have a maximum peak amplitude of 8192, and the data is offset such that -8192 in your binary data is 0 in the device; and +8192 is 16384.
This means that only 14 bits in the 16-bit bin value is used.

Waveforms made in Waveform Manager therefore needs to have a maximum amplitude of 8192. 
In the tool, amplitude means peak-to-peak amplitude.

The waveform should then be saved as a wfm file. 
