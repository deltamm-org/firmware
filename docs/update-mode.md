# Update mode

The D878UVII's boot loader (the first 16 KB of MCU flash) can rewrite the
firmware image above it. `dmm-radio flash` speaks its protocol, so a
radio can be flashed from Linux, macOS or Windows.

## Entering update mode

Switch the radio off. Hold **PTT and PF3** (the top key) and switch it on.
The display stays dark and the red LED blinks. The radio shows up as the
same USB serial device as in normal use (USB IDs 28e9:018a).

PTT and PF1 at power-on is a different start-up: the factory firmware asks
"Initialize the radio?". Press Exit there.

## Using `dmm-radio flash`

```sh
dmm-radio flash <file.CDD>
```

The `.spi` and `.CDI` files must sit next to the `.CDD` file with the same
name. Any D878UVII firmware set works: DeltaMM's update files from
`dmm-radio prepare` (or the `update-files` build target), or the factory
files, to go back to the factory firmware.

Before sending anything, the tool checks that:

- a verified backup of the radio exists (the same setting as for
  `prepare`, `dmm-radio config backup-before-flash`);
- the `.spi` header names the D878UVII, the `.CDI` header loads the
  image, and both give the `.CDD` file's length;
- the image starts with a stack pointer in RAM and a reset handler inside
  the image, and ends below the radio data;
- the radio answers in update mode and reports the model `D878UV2`.

With `--wait <seconds>`, the tool first waits that long for a radio to
appear in update mode, so it can be started before the radio is switched
on. A radio running its factory firmware has the same USB IDs as one in
update mode (DeltaMM's ports also say `DeltaMM` as their serial number),
so a radio already connected when the wait starts has to be
switched off and back on in update mode first.

After the transfer ends, the radio starts the new firmware by itself, and
the tool waits up to 15 s for DeltaMM to answer on USB and prints its
version.

If the transfer stops, the tool names the address where it stopped. The
boot loader is never written, so the radio stays in update mode: run
`flash` again with the same files or with the factory ones.
