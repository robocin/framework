#!/bin/sh
while true; do
		bin/ra -t
		if [ $? -eq 0 ]; then
				break;
		fi
		echo $?
done
