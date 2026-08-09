# Contributing

## Ground Rules

- Drivers must be written from public datasheets, public specifications, or clean room reverse engineering notes that are included alongside the driver. Do not paste in code copied from another project unless the license is compatible and it is attributed clearly.
- No OS-specific code in drivers/. If a driver needs something nc_platform does not provide, open an issue proposing the addition to the interface, do not special case around it.
- Keep pull requests scoped to one driver or one interface change at a time.

## Adding a New Driver

1. Pick a device class folder under drivers/, for example drivers/net, drivers/storage. Create a new one if the class does not exist yet.
2. Write the driver against include/nc/platform.h and include/nc/device.h only.
3. Include a REFERENCES.md next to the driver listing the datasheet or spec used, with links and revision numbers.
4. Register the driver struct through nc_register_net_driver or the equivalent for your device class.
5. If you had to test against real hardware, note the exact model and revision in REFERENCES.md, since chip revisions frequently change behavior.

## Review Process

Maintainers review for interface correctness first, meaning no OS assumptions leaking into driver code, and correctness against the cited spec second. Functional testing on real hardware is encouraged but not always required for initial merge, incomplete drivers marked with TODO are welcome as long as the structure is right.

## Code Style

### General Principles

- **Clarity over cleverness**: Write code that is easy to understand and maintain. Future contributors should be able to grasp the logic quickly.
- **Consistency**: Follow the existing patterns in the codebase. If a similar pattern exists elsewhere, use it rather than inventing a new approach.
- **Minimalism**: Only add what is necessary. Avoid over-engineering solutions for simple problems.
- **Platform abstraction**: Never assume specific OS behavior. All hardware access must go through nc_platform primitives.

### Formatting

- Use lowercase file and folder names.
- Match the existing formatting in the file you are editing rather than reformatting unrelated code.
- Use 4-space indentation for C code.
- Keep lines under 80 characters when practical, but prioritize readability over strict adherence.

### Comments

- Comments should explain **why**, not **what**. The code itself should show what is happening.
- Use comments for:
  - TODO markers for incomplete functionality
  - References to specific sections of datasheets or specifications
  - Non-obvious hardware quirks or workarounds
  - Complex algorithm explanations that cannot be made clearer through refactoring
- Avoid comments that simply restate the code, such as "increment i" next to `i++`.
- Keep comments concise and to the point.

### Naming Conventions

- Use snake_case for functions and variables: `dma_alloc`, `tx_avail_idx`
- Use SCREAMING_SNAKE_CASE for constants and macros: `VIRTIO_PCI_VENDOR_ID`, `NC_MAX_DRIVERS`
- Use descriptive names that convey purpose: `rx_used_idx` is better than `rui`
- Avoid abbreviations unless they are widely understood: `mmio` is fine, but `ctx` should be `context`

### Error Handling

- Return negative values for errors, 0 or positive for success
- Check return values from platform functions
- Log errors through nc_platform->log when appropriate
- Clean up resources on error paths

### Memory Management

- Always pair alloc with free
- Use dma_alloc for device-accessible memory
- Never assume virtual and physical addresses are the same
- Document ownership of pointers in struct comments

### Testing Considerations

- Write code that works both with interrupts and polling
- Use locks for shared state between interrupt and main thread
- Consider how the driver will behave under QEMU emulation
- Document any hardware-specific testing requirements in REFERENCES.md
