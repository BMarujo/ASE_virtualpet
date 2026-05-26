import sys

ST7735_BLACK   = "0x0000"
ST7735_WHITE   = "0xFFFF"
ST7735_RED     = "0xF800"
ST7735_CYAN    = "0x07FF"
ST7735_GRAY    = "0x7BEF"

color_map = {
    ' ': ST7735_BLACK,
    'b': ST7735_CYAN,
    'e': ST7735_WHITE,
    'p': ST7735_BLACK,
    'm': ST7735_RED,
    'z': ST7735_GRAY,
    'y': "0xFFE0" # yellow
}

def ascii_to_c_array(name, ascii_art):
    lines = [l for l in ascii_art.split('\n') if len(l) > 0]
    height = len(lines)
    width = max(len(l) for l in lines)
    out = f"const uint16_t pet_{name}[{width * height}] = {{\n"
    for line in lines:
        padded_line = line.ljust(width)
        row = []
        for char in padded_line:
            row.append(color_map.get(char, ST7735_BLACK))
        out += "    " + ", ".join(row) + ",\n"
    out += "};\n\n"
    return out, width, height

happy_ascii = """\
        bbbbbbbbbbbbbbbb        
      bbbbbbbbbbbbbbbbbbbb      
    bbbbbbbbbbbbbbbbbbbbbbbb    
   bbbbbbbbbbbbbbbbbbbbbbbbbb   
  bbbbbbbbbbbbbbbbbbbbbbbbbbbb  
  bbbbbbbeeebbbbbbbbeeebbbbbbb  
 bbbbbbbeeeeeebbbbeeeeeebbbbbbb 
 bbbbbbbepepeebbbbepepeebbbbbbb 
 bbbbbbbeeeeeebbbbeeeeeebbbbbbb 
 bbbbbbbbeeebbbbbbbbeeebbbbbbbb 
 bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb 
 bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb 
 bbbbbbbbbbbmmmmmmmmbbbbbbbbbbb 
 bbbbbbbbbbbbmmmmmmbbbbbbbbbbbb 
  bbbbbbbbbbbbbbbbbbbbbbbbbbbb  
  bbbbbbbbbbbbbbbbbbbbbbbbbbbb  
   bbbbbbbbbbbbbbbbbbbbbbbbbb   
    bbbbbbbbbbbbbbbbbbbbbbbb    
      bbbbbbbbbbbbbbbbbbbb      
        bbbbbbbbbbbbbbbb        
        bbbb        bbbb        
        bbbb        bbbb        \
"""

sad_ascii = """\
        bbbbbbbbbbbbbbbb        
      bbbbbbbbbbbbbbbbbbbb      
    bbbbbbbbbbbbbbbbbbbbbbbb    
   bbbbbbbbbbbbbbbbbbbbbbbbbb   
  bbbbbbbbbbbbbbbbbbbbbbbbbbbb  
  bbbbbbbeeebbbbbbbbeeebbbbbbb  
 bbbbbbbeeeeeebbbbeeeeeebbbbbbb 
 bbbbbbbeeepeebbbbeeepeebbbbbbb 
 bbbbbbbeeeeeebbbbeeeeeebbbbbbb 
 bbbbbbbbeeebbbbbbbbeeebbbbbbbb 
 bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb 
 bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb 
 bbbbbbbbbbbbmmmmmmbbbbbbbbbbbb 
 bbbbbbbbbbbmmmmmmmmbbbbbbbbbbb 
  bbbbbbbbbbbbbbbbbbbbbbbbbbbb  
  bbbbbbbbbbbbbbbbbbbbbbbbbbbb  
   bbbbbbbbbbbbbbbbbbbbbbbbbb   
    bbbbbbbbbbbbbbbbbbbbbbbb    
      bbbbbbbbbbbbbbbbbbbb      
        bbbbbbbbbbbbbbbb        
        bbbb        bbbb        
        bbbb        bbbb        \
"""

sleep_ascii = """\
           z                    
             z                  
         bbbbbbbbbbbbbbbb       
       bbbbbbbbbbbbbbbbbbbb     
     bbbbbbbbbbbbbbbbbbbbbbbb   
    bbbbbbbbbbbbbbbbbbbbbbbbbb  
   bbbbbbbbbbbbbbbbbbbbbbbbbbbb 
   bbbbbbbbbbbbbbbbbbbbbbbbbbbb 
  bbbbbbbbbbpepebbbbbbpepebbbbbb
  bbbbbbbbbbpepebbbbbbpepebbbbbb
  bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
  bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
  bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
  bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
  bbbbbbbbbbbbbbmmbbbbbbbbbbbbbb
   bbbbbbbbbbbbbbbbbbbbbbbbbbbb 
   bbbbbbbbbbbbbbbbbbbbbbbbbbbb 
    bbbbbbbbbbbbbbbbbbbbbbbbbb  
     bbbbbbbbbbbbbbbbbbbbbbbb   
       bbbbbbbbbbbbbbbbbbbb     
         bbbbbbbbbbbbbbbb       
         bbbb        bbbb       
         bbbb        bbbb       \
"""

h_content = "#pragma once\n#include <stdint.h>\n\n"
h_content += f"#define PET_WIDTH 32\n"
h_content += f"#define PET_HEIGHT 23\n\n"
h_content += "extern const uint16_t pet_happy[];\n"
h_content += "extern const uint16_t pet_sad[];\n"
h_content += "extern const uint16_t pet_sleep[];\n"

c_content = '#include "pet_assets.h"\n\n'
c1, w, h = ascii_to_c_array("happy", happy_ascii)
c2, _, _ = ascii_to_c_array("sad", sad_ascii)
c3, _, _ = ascii_to_c_array("sleep", sleep_ascii)

c_content += c1 + c2 + c3

with open("/home/marujo/MECT/ASE/pratica/project/virtual-pet/main/pet_assets.h", "w") as f:
    f.write(h_content)

with open("/home/marujo/MECT/ASE/pratica/project/virtual-pet/main/pet_assets.c", "w") as f:
    f.write(c_content)

print("Generated pet_assets.h and pet_assets.c")
