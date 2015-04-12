# raspberry-pi-kWh
Raspberry Pi kWh kernel module.


## Build

```sh
export KERNEL_ROOT=<KERNEL-SRC-BASE-FOLDER>/linux
```
and

```sh
make
```

## Usage

### Configuration

One way to load the module when the system is started is to add the the command below into rc.local located in the /etc folder
```sh
/etc/rc.local
/sbin/insmod /home/pi/powermod.ko
```

The device id in the message can be altered with an optional module paramter, Ex: device_id="main-pwr"

```sh
#!/bin/bash

data=$(cat /sys/kernel/power-mod/json_ev)
host=<service-ip>
port=<service-port>
curl -v -H 'Accept: application/json' -H 'Content-Type: application/json' -d  @/sys/kernel/power-mod/json_ev -X POST http://${host}:${port}/data
```

To send the value to a central server/service at an regular interval, run the script above from the cron daemon.
Add the entry below that will execute every minute.
```sh
* * * * * /home/pi/do_send_power > /dev/null 2>&1
```

## License

Copyright (c) 2015 JanneLindberg

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
