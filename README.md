# SonarScannerV1

Code for Sonar Scanner Video:
https://youtu.be/z4uxC7ISd-c

The arduino code is for an ESP32. The scanned field is sent over Serial.
The code is designed to run on two cores to upload the frame while scanning is recoding.
However, I discovered that the serial communication creates artifacts in the scan.
Currently the Tasks are set to wait for each other to complete. I'll rewrite that soon.

Important. change the core setting to 0 for "Event" and "Arduino". This won't be necessary when the tasks will be merged again.
