// Variables
// All dimensions in millimeters

// --- BASE (Single Piece, Open Bottom, 1.15x Scale, 1.6mm Walls) ---
base_w = 69;       
base_d = 57.5;     
base_h = 28.75;    
base_wall = 1.6;   

inner_w = base_w - (2 * base_wall);
inner_d = base_d - (2 * base_wall);

// --- NECK ---
neck_segments = 5;
neck_segment_h = 10;
neck_segment_r = 6; 
neck_bend = 10; 

// --- HEAD WITH CONNECTORS CONFIG ---
head_w = 50;
head_d = 50;
head_h = 45;
head_wall = 3;

// Pins & Sockets dimensions
pin_r = 0.9;         // 1.8mm total diameter for the pin
pin_h = 4;           // Pin length
socket_r = 1.1;      // 2.2mm diameter (0.2mm tolerance gap around the pin)
socket_h = 4.5;      // Slightly deeper than the pin for flush fit

screen_w = 17; 
screen_h = 11;
screen_margin = 1; 

amp_r = 8; 
speaker_r = 10; 
comp_space = 25; 

// Derived values
neck_h = neck_segments * neck_segment_h;
wheel_r = 8;
wheel_d = 5;
head_angle = 15;

$fn = 64;

// --- Modules ---

// 1. Integrated Base Enclosure
module base_single_piece() {
    difference() {
        union() {
            cube([base_w, base_d, base_h], center=true);
            translate([-(base_w/2), -(base_d/2 - 5), -2.5]) cube([base_w+2, 10, 5]);
        }
        translate([0, 0, -base_wall])
            cube([inner_w, inner_d, base_h], center=true);
        translate([-base_w/2 - 1, -(base_d/2 - 7.5), -5]) cube([15, 5, 10]);
        cylinder(r=neck_segment_r + 1, h=base_h+2, center=true);
    }
}

// 2. Wheels
module wheel() {
    color("black") {
        rotate([0, 90, 0])
            cylinder(r=wheel_r, h=wheel_d, center=true);
    }
}

// 3. Articulated Neck
module neck() {
    union() {
        for (i = [0:neck_segments-1]) {
            translate([0, sin(i * neck_bend) * neck_segment_h, i * neck_segment_h])
            rotate([-(i-neck_segments/2)*neck_bend/neck_segments, 0, 0]) {
                difference() {
                    cylinder(r1=neck_segment_r, r2=neck_segment_r+0.5, h=neck_segment_h, center=true);
                    cylinder(r=neck_segment_r - 2, h=neck_segment_h + 2, center=true);
                }
            }
        }
    }
}

// 4. Head (Body) - Updated with Female Sockets
module head_body() {
    difference() {
        // Outer Body
        union() {
            cube([head_w, head_d, head_h], center=true);
            for (side = [-1, 1]) {
                translate([side * (head_w/2 + 2), 0, 0])
                    cube([4, head_d - 10, head_h - 10], center=true);
                for (y = [-1, 1], z = [-1, 1]) {
                    translate([side * (head_w/2 + 3), y * (head_d/2 - 15), z * (head_h/2 - 15)])
                        rotate([0, 90, 0])
                            cylinder(r=2, h=4, center=true);
                }
            }
        }
        
        // Screen cutting
        translate([0, -(head_d/2 - 1), head_h/2 - screen_h/2 - head_wall - 10]) {
            cube([screen_w + screen_margin*2, head_wall*2, screen_h + screen_margin*2], center=true);
        }
        
        // Main internal electronics cavity
        translate([0, (head_wall)/2, 0])
            cube([head_w - head_wall*2, head_d - head_wall, head_h - head_wall*2], center=true);
            
        // Screen mount internal frame
        translate([0, -(head_d/2 - head_wall/2 - 5), head_h/2 - screen_h/2 - head_wall - 10]) {
            cube([screen_w + 10, 5, screen_h + 10], center=true);
        }

        // Speaker mount
        translate([0, comp_space/2 - head_d/2 + head_wall + 15, -head_h/2 + head_wall + 10]) {
            cylinder(r=speaker_r + 2, h=10, center=true);
            cylinder(r=speaker_r, h=12, center=true);
        }
        
        // Amp mount
        translate([-(head_w/2 - head_wall - 10), comp_space/2 - head_d/2 + head_wall + 10, -head_h/2 + head_wall + 10]) {
            cube([amp_r * 2 + 2, amp_r * 2 + 2, 10], center=true);
        }
        
        // Neck wire entry hole
        translate([0, 0, -head_h/2])
            cylinder(r=neck_segment_r + 1, h=head_wall*2, center=true);

        // --- Connector Sockets (Female Holes) ---
        // Positioned precisely in the center of the 3mm rear walls
        for (x = [-1, 1], z = [-1, 1]) {
            translate([x * (head_w/2 - head_wall/2), head_d/2 - socket_h/2 + 0.1, z * (head_h/2 - head_wall/2)])
                rotate([90, 0, 0])
                    cylinder(r=socket_r, h=socket_h, center=true);
        }
    }
}

// 5. Head Lid - Updated with Male Pins
module head_lid() {
    union() {
        // Main flat lid plate
        cube([head_w, head_wall, head_h], center=true);
        
        // --- Connector Pins (Male) ---
        // Extrudes forward from the lid into the body's sockets
        for (x = [-1, 1], z = [-1, 1]) {
            translate([x * (head_w/2 - head_wall/2), -head_wall/2, z * (head_h/2 - head_wall/2)])
                rotate([-90, 0, 0])
                    cylinder(r=pin_r, h=pin_h);
        }
    }
}


// --- EXPORT CONTROLS ---
// בשביל לייצא חלק ספציפי, מחק את ה- '//' לפניו (וודא שהאחרים חסומים):

// base_single_piece();
 head_body();
// head_lid();

/* // --- ASSEMBLED PREVIEW (for viewing) ---
union() {
    translate([0,0, base_h/2]) {
        color("lightgray") base_single_piece();
    }
    
    for (x = [-1, 1], y = [-1, 1]) {
        translate([x * (base_w/2 - 12), y * (base_d/2 - 12), -(wheel_r + 1)]) wheel();
    }
    
    translate([0,0, base_h]) {
        color("silver") neck();
    }
    
    translate([0, neck_bend, base_h + neck_h])
    rotate([head_angle, 0, 0]) {
        color("steelblue") head_body();
        translate([0, head_d/2 + head_wall/2, 0]) color("lightsteelblue") head_lid();
    }
}
*/