function Encoder(object, port) {
  var bytes = [];

  // Measurement resolution
  if (port === 1) {
    if (object.resolution === 0 || object.resolution === 1) {
      bytes[0] = object.resolution;
    }
  }

  // Sleep time
  if (port === 2) {
    if (object.sleep >= 3) {
      bytes[0] = (object.sleep & 0xFF00) >> 8;
      bytes[1] = (object.sleep & 0x00FF);
    }
  }

  // System reset
  if (port === 3) {
    if (object.reset === 1) {
      bytes[0] = object.reset;
    }
  }

  // Output data
  if (port === 4) {
    if (object.out0 === 1) {
      bytes[0] |= 1;
    }
    if (object.out1 === 1) {
      bytes[0] |= 2;
    }
    if (object.out2 === 1) {
      bytes[0] |= 4;
    }
    if (object.out3 === 1) {
      bytes[0] |= 8;
    }
  }

  return bytes;
}