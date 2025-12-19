// Case for Android LoRa BLE Bridge Firmware MCU
// Author: Gemini
// Date: 2025-12-19
// Units: mm

/* [Print Settings] */
// Select part to render
render_part = "both"; // [box, lid, both]
// Show mock components for fit check
show_components = true; 
// Smoothing
$fn = 60;

/* [Dimensions] */
wall = 2.0;
// General clearance for fit
clearance = 0.5;
corner_radius = 4.0;

// Components
// Heltec Wireless Stick Lite V3: ~58 x 23 x 8.2 mm
mcu_dims = [58.5, 23.0, 9.0]; 

// 18650 Battery Holder: ~76 x 21 x 18 mm
bat_dims = [76.5, 21.5, 18.5];

// Layout: Side by Side
// Add space for cables
cable_space = 20.0; 

// Internal Dimensions calculation
// Length: Determined by battery (longest part) + cable space
inner_x = bat_dims[0] + cable_space; 
// Width: Battery width + MCU width + internal divider/gap
inner_y = bat_dims[1] + mcu_dims[1] + 2.0; 
// Height: Determined by Battery (tallest part) + clearance
inner_z = bat_dims[2] + 1.0; 

// Outer dimensions
outer_x = inner_x + 2*wall;
outer_y = inner_y + 2*wall;
outer_z = inner_z + wall; // Box height

// Screw Posts (M3 x 20mm)
screw_d = 3.4; // Clearance hole for M3
nut_d = 6.4;   // M3 Nut diameter (corner-to-corner loose fit)
nut_h = 3.0;   // M3 Nut thickness
head_d = 6.0;  // M3 Screw head diameter

// Nut Trap Elevation
// Since case is ~23mm high and screw is 20mm, the screw tip only reaches ~3mm from bottom.
// We must raise the nut to meet the screw.
nut_elevation = 8.0; // Height of the nut slot floor from bottom

// Component Cutouts
ant_d = 6.8;   // SMA connector (6.5mm actual)
switch_dims = [6.5, 6.5]; // DIP switch

module round_rect(x, y, z, r) {
    linear_extrude(height=z)
    offset(r=r)
    square([x-2*r, y-2*r], center=true);
}

module mock_components() {
    // Battery Holder (Left side, -Y)
    // Centered in X to avoid posts
    translate([0, -inner_y/4 - 1, wall + bat_dims[2]/2]) {
        color("Blue") cube(bat_dims, center=true);
        // Add fake battery cylinder
        translate([0,0,0]) rotate([0,90,0]) color("CornflowerBlue") cylinder(h=65, r=9, center=true);
    }

    // MCU (Right side, +Y)
    // Centered in X
    translate([0, inner_y/4 + 1, wall + mcu_dims[2]/2]) {
        color("Green") cube(mcu_dims, center=true);
        // SMA Connector Mockup
        translate([mcu_dims[0]/2, 0, 0]) rotate([0,90,0]) color("Gold") cylinder(h=10, r=3);
    }
    
    // DIP Switch (End Face - 1/3 Position)
    translate([outer_x/2 - 2, -inner_y/6, wall + 4])
        color("Red") cube([6, switch_dims[0], switch_dims[1]], center=true);

    // Panel Mount SMA (End Face - 1/3 Position)
    translate([outer_x/2 - 2, inner_y/6, inner_z/2 + wall/2])
         rotate([0,90,0]) color("Gold") cylinder(h=8, r=3.5, center=true);
}

module lower_case() {
    difference() {
        union() {
            // 1. Hollow Shell
            difference() {
                // Main Block (starts at Z=0)
                round_rect(outer_x, outer_y, outer_z, corner_radius);
                
                // Inner Hollow (starts at Z=wall)
                translate([0,0,wall])
                    round_rect(inner_x, inner_y, outer_z, corner_radius - wall/2);
            }
                
            // 2. Corner Posts (Solid, added back into the corners)
            // Flush with outer radius (r = corner_radius)
            post_offset_x = outer_x/2 - corner_radius;
            post_offset_y = outer_y/2 - corner_radius;
            for(px=[-1,1]) for(py=[-1,1]) {
                translate([px*post_offset_x, py*post_offset_y, 0])
                    cylinder(h=outer_z, r=corner_radius); 
            }
        }
        
        // 3. Subtract Screw Holes & Nut Traps from the Union
        post_offset_x = outer_x/2 - corner_radius;
        post_offset_y = outer_y/2 - corner_radius;
        for(px=[-1,1]) for(py=[-1,1]) {
            translate([px*post_offset_x, py*post_offset_y, 0]) {
                // Vertical Screw hole
                translate([0,0,-1]) cylinder(h=outer_z+2, r=screw_d/2);
                
                // Side Nut Slot
                translate([0, 0, nut_elevation + nut_h/2]) {
                    // Nut hex shape
                    rotate([0,0,30]) cylinder(h=nut_h, r=nut_d/2, $fn=6, center=true);
                    
                    // Slot to insert nut
                    // Slot direction depends on corner to point inwards
                    rotate([0,0, (py > 0 ? -90 : 90)]) 
                        translate([5, 0, 0])
                        cube([14, nut_d * 0.866, nut_h], center=true); 
                }
            }
        }
        
        // --- CUTOUTS ---
        
        // 1. Antenna (SMA) - 1/3 Position (+Y side)
        translate([outer_x/2, inner_y/6, inner_z/2 + wall/2])
            rotate([0,90,0])
            cylinder(h=15, r=ant_d/2, center=true);
            
        // 3. DIP Switch (End Face) - 1/3 Position (-Y side)
        translate([outer_x/2, -inner_y/6, wall + 4])
            cube([15, switch_dims[0], switch_dims[1]], center=true);
    }
}

module lid() {
    lid_thickness = wall;
    difference() {
        union() {
            // Lid Plate (starts at Z=0)
             round_rect(outer_x, outer_y, lid_thickness, corner_radius);
        }
        
        // Screw Holes
        post_offset_x = outer_x/2 - corner_radius;
        post_offset_y = outer_y/2 - corner_radius;
        for(px=[-1,1]) for(py=[-1,1]) {
            translate([px*post_offset_x, py*post_offset_y, -1]) {
                cylinder(h=lid_thickness+2, r=screw_d/2);
                // Countersink
                translate([0,0,lid_thickness - 1.5]) 
                    cylinder(h=2.6, r1=screw_d/2, r2=head_d/2 + 0.5);
            }
        }
    }
    
    // Lip to fit inside box
    // Extrude downwards from the lid bottom (Z=0)
    translate([0,0, -2.0])
        difference() {
            round_rect(inner_x - 0.4, inner_y - 0.4, 2.0, corner_radius - wall/2);
            translate([0,0,-0.5])
                round_rect(inner_x - 2.4, inner_y - 2.4, 3.0, corner_radius - wall/2 - 1); // Hollow
        }
}

// Render Logic
if (show_components) {
    mock_components();
}

if (render_part == "box") {
    lower_case();
} else if (render_part == "lid") {
    lid();
} else {
    lower_case();
    translate([0, outer_y + 5, 0]) lid();
}