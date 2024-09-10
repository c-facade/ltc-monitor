# LTCSENSORS

`ltcsensors.py` is a small script that makes use of the ltc3350 driver to get data about its sensors.

It allows the user to read values of capacitor voltages, die temperature, and other ltc3350 voltages. These can be printed in real time (the values are updated every seconds) and stored in a csv file.
Reference values (that is the alarm levels) are also printed but are not stored in the csv file.

This is its usage:
```
usage: ltcsensors [-h] [-s] [-l LENGTH] [-f FILE]

Offers readings of ltc3350 sensors

options:
  -h, --help            show this help message and exit
  -s, --silent					stop all prints except a "done" print (good for background operation)
  -l LENGTH, --length LENGTH		number of seconds to execute the program
  -f FILE, --file FILE		output csv file
```

The values are converted into standard measurement units.
