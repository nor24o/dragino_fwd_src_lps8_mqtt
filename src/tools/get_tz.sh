#!/bin/bash
timezone=$(curl -fs ipinfo.io | grep "timezone" | awk -F\" '{print $4}')

echo "timezone=$timezone"
