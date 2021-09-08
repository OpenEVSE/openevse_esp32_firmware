#!/bin/sh

exec npm start -- --port ${PORT:=3000} $@
