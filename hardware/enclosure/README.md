# 3D-Printable Enclosure

3D models for the physical robot body that houses the ESP32-S3, OLED screen, microphone, and speaker/amp.

## Files

- `Karen.scad` - OpenSCAD source; parametric model with base, neck, and head (screen/amp/speaker cutouts, pin-and-socket connectors). Edit the variables at the top to adjust dimensions.
- `karen base plate.stl` - Bottom base plate
- `Base Cart.stl` - Base/cart body (open-bottom enclosure)
- `Wheel.stl` - Wheel
- `Neck.stl` - Segmented neck connecting base to head
- `Neck Holder.stl` - Neck mounting bracket
- `head_body.stl` - Head shell (screen/amp/speaker cutouts)
- `head_lid.stl` - Head lid/cover

## Printing

All dimensions in millimeters, sized for FDM printing. Open `Karen.scad` in [OpenSCAD](https://openscad.org/) to tweak dimensions or re-export STLs.
