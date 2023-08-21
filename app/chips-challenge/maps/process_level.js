const fs = require("fs");


var outfile = fs.openSync("map_data.bin", "w");

for (let i=1; i<12; i++) {
  var data = fs.readFileSync(`tmp/level-${i}.json`, "ascii");
  var obj = JSON.parse(data);
  var data = Array(32*32).fill(0);
  var layer_lut = {};
  for (var key in obj.layers) {
    var l = obj.layers[key];
    layer_lut[l.name] = l;
  }
  for (var row = 0; row < 32; row++) {
    if (row >= obj.height) continue;
    for (var col = 0; col < 32; col++) {
      if (col >= obj.width) continue;
      var tile = layer_lut["map"].data[(row * obj.width)+col];
      data[(row*32) + col] = tile;
    }
  }
  //console.log(Buffer.from(data));
  fs.writeFileSync(outfile, Buffer.from(data));
  console.log(`processed level ${i}`);
}
