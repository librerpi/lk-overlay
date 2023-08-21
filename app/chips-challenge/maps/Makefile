tmp/level%.json: level%.tmx
	mkdir -p tmp
	tiled --export-map $< $@

.PHONY: map_data.bin
map_data.bin: tmp/level-1.json tmp/level-2.json tmp/level-3.json tmp/level-4.json tmp/level-5.json tmp/level-6.json tmp/level-7.json tmp/level-8.json tmp/level-9.json tmp/level-10.json tmp/level-11.json
	node process_level.js
