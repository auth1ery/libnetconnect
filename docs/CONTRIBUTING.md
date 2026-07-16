# contributing

## ground rules

- drivers must be written from public datasheets, public specifications, or clean room reverse engineering notes that are included alongside the driver. do not paste in code copied from another project unless the license is compatible and it is attributed clearly.
- no os specific code in drivers/. if a driver needs something nc_platform does not provide, open an issue proposing the addition to the interface, do not special case around it.
- keep pull requests scoped to one driver or one interface change at a time.

## adding a new driver

1. pick a device class folder under drivers/, for example drivers/net, drivers/storage. create a new one if the class does not exist yet.
2. write the driver against include/nc/platform.h and include/nc/device.h only.
3. include a REFERENCES.md next to the driver listing the datasheet or spec used, with links and revision numbers.
4. register the driver struct through nc_register_net_driver or the equivalent for your device class.
5. if you had to test against real hardware, note the exact model and revision in REFERENCES.md, since chip revisions frequently change behavior.

## review process

maintainers review for interface correctness first, meaning no os assumptions leaking into driver code, and correctness against the cited spec second. functional testing on real hardware is encouraged but not always required for initial merge, incomplete drivers marked with TODO are welcome as long as the structure is right.

## code style

- no code comments explaining the obvious, comments are for TODOs, spec references, and non obvious hardware quirks only.
- lowercase file and folder names.
- match the existing formatting in the file you are editing rather than reformatting unrelated code.
