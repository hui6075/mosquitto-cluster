#!/bin/bash

if [ "$TRAVIS_OS_NAME" == "osx" ]; then
	cmake .
fi
