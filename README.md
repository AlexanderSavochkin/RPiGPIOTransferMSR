## Testing parralel data transfer from external device to Raspberry Pi 4

The repository contains the code related to my blog post [my blog post](https://alexandersavochkin.github.io/posts/fpga-driven-data-streaming-into-raspberry-pi-speed-and-timing-stability-part-1/) .

It includes the logic definition for the iCEstick FPGA evaluation board, which generates data, and C code for the Raspberry Pi 4, which reads the data stream from the FPGA using a "parallel bus" implemented with GPIO pins.

The purpose of the project is to serve as a proof of concept, demonstrating the feasibility of this method for high-speed data transfer into the Raspberry Pi and measuring the achievable speeds.
