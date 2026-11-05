# Lisa GT9897 input investigation

`gt9897_frame.c` is an offline, transport-independent prototype for decoding
Berlin-A touch frames. It performs no hardware access and is not integrated
into XNU or iOS. Its protocol reference is Linux's
[goodix_berlin_core.c](https://github.com/torvalds/linux/blob/master/drivers/input/touchscreen/goodix_berlin_core.c),
which is GPL-2.0-or-later; check license compatibility before integrating it
into any XNU distribution. The caller must validate and assemble the SPI reads,
especially the Linux driver's split reads for more than two contacts, and must
acknowledge events appropriately. No real Lisa frame has been tested.

Run host checks with:

```
cc -std=c11 -Wall -Wextra -Werror gt9897_frame.c test_gt9897.c -o /tmp/gt9897-test
/tmp/gt9897-test
```

Remaining work includes Qualcomm GENI SPI setup, bounded transfers, IRQ and
reset handling, RPMh power management, iOS input-event integration, and
hardware testing. Do not use Sweet's I2C or regulator code unchanged.

`lisa_goodix_spi.c` is a host-tested Berlin-A framing adapter for a future
full-duplex SPI transport. Reads are bounded, and writes are restricted to a
zero acknowledgement of the configured event address. The callback must hold
chip select for the entire transfer. This code is not integrated into XNU and
has not been tested on the phone; it does not configure GENI, clocks or power.
Run its simulated-bus test with:

```
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  lisa_goodix_spi.c test_lisa_goodix_spi.c -o /tmp/lisa-goodix-spi-test
/tmp/lisa-goodix-spi-test
```
