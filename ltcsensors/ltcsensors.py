#!/usr/bin/env python3
import csv
import os
from time import sleep, gmtime, strftime
import argparse

DIRECTORY = '/sys/bus/i2c/devices/i2c-2/2-0009/hwmon/hwmon4'
#CSV_FILE = 'dynagate2_1h_capesr2min.csv'

def LSB_to_millivolts(value, conversion_factor):
    millivolts = value * conversion_factor / 1000;
    return f"{millivolts:5.0f} mV"

def LSB_to_celsius(value):
    celsius = 0.028 * value - 251.4
    return f"{celsius:5.1f} °C"


def convert_from_LSB(buf, name):
    try:
        value = float(buf)
    except ValueError:
        print("ValueError")
        return "Error"
    if "dtemp" in name:
        return LSB_to_celsius(value)
    if name in ["vcap1", "vcap2", "vcap3", "vcap4", "cap_ov_lvl", "cap_uv_lvl"]:
        return LSB_to_millivolts(value, 183.5)
    if "gpi" in name:
        return LSB_to_millivolts(value, 183.5)
    if "vcap" in name:
        return LSB_to_millivolts(value, 1476)
    if "iic" in name:
        return value
    return LSB_to_millivolts(value, 2210)


def sensors(silent, filename):
    files = os.listdir(DIRECTORY)
    sorted_files = sorted(files)
    row = []
    timestamp = strftime("%H:%M:%S", gmtime())
    row.append(timestamp)

    for file in sorted_files:
        file_path = os.path.join(DIRECTORY, file)
        max_name = ""
        min_name = ""
        meas_val = ""
        meas_name = ""
        max_val = ""
        min_val = ""
        if os.path.isfile(file_path) and file.startswith('meas_'):
            with open(file_path, 'r') as f:
                meas_name = file.split("_", 1)[1]
                meas_val = convert_from_LSB(f.read().strip(), meas_name)
                if meas_name in ["vcap1", "vcap2", "vcap3", "vcap4"]:
                    max_name = "cap_ov_lvl"
                    min_name = "cap_uv_lvl"
                elif meas_name == "dtemp":
                    max_name = "dtemp_hot_lvl"
                    min_name = "dtemp_cold_lvl"
                elif meas_name in ["gpi", "vin", "vcap", "vout"]:
                    max_name = meas_name+"_ov_lvl"
                    min_name = meas_name+"_uv_lvl"
                elif meas_name == "iin":
                    max_name = "iin_oc_lvl"
                    min_name = "iin_uc_lvl"
                else:
                    continue
        else:
            continue

        row.append(meas_val);
        max_path = os.path.join(DIRECTORY, max_name)
        if os.path.isfile(max_path):
            with open(max_path, 'r') as f:
                max_val = convert_from_LSB(f.read().strip(), max_name)

        min_path = os.path.join(DIRECTORY, min_name)
        if os.path.isfile(min_path):
            with open(min_path, 'r') as f:
                min_val = convert_from_LSB(f.read().strip(), max_name)
        if(not silent):
           print(f"{meas_name}:\t{meas_val}\t\t(max = {max_val}, min = {min_val})")
    with open(filename, 'a', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(row)

def sensorsc():
    LINE_UP = '\033[1A'
    LINE_CLEAR = '\x1b[2K'
    
    parser = argparse.ArgumentParser(
            prog="ltcsensors",
            description="Offers readings of ltc3350 sensors",
            );
    parser.add_argument('-s', '--silent', action='store_true')
    parser.add_argument('-l', '--length', default=120);
    parser.add_argument('-f', '--file', default="dynagate2_report.csv");

    args = parser.parse_args()

    with open(args.file, 'w', newline="") as file:
        writer = csv.writer(file)
        writer.writerow(['timestamp', 'dtemp', 'iin', 'gpi', 'vcap', 'vcap1', 'vcap2', 'vcap3', 'vcap4', 'vin', 'vout'])
    for _ in range(int(args.length)):
        sensors(args.silent, args.file)
        sleep(1)
        if(not args.silent):        
                for _ in range(10):
                        print(LINE_UP, end=LINE_CLEAR)


if __name__ == '__main__':
    sensorsc()
