from luma.core.render import canvas
from PIL import ImageFont, Image
from enum import Enum
import math
import os

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS_DIR = os.path.join(BASE_DIR, 'assets')
FONTS_DIR = os.path.join(ASSETS_DIR, 'fonts')

class Icon:
  HEART = bytearray([0x00,0x6C,0xFE,0xFE,0xFE,0x7C,0x38,0x10])
  CONFIRM = bytearray([0x00, 0x01, 0x03, 0x86, 0xcc, 0x78, 0x30, 0x00])
  NOTE = bytearray([0x3f, 0x21, 0x21, 0x21, 0x63, 0xe7, 0xe7, 0x42])
  CHAT = bytearray([0xff, 0xff, 0xff, 0xff, 0xff, 0x1c, 0x0c, 0x04])
  CANCEL = bytearray([0x7e, 0x5a, 0x5a, 0x7e, 0x24, 0x81, 0x7e, 0x81])
  OK = bytearray([0xFF, 0xFF,0xF1, 0xB7,0xEE, 0xAF,0xEE, 0x9F,0xEE, 0xAF,0xF1, 0xB7,0xFF, 0xFF,0x00, 0x00])
  BACK = bytearray([0x36, 0xD8,0x76, 0xD8,0xC0, 0x00,0xC0, 0x00,0xC0, 0x02,0x76, 0xDF,0x36, 0xDF,0x00, 0x02])
  DEL = bytearray([0xFF, 0xFD,0xC6, 0x2A,0xDA, 0xED,0xDA, 0x2A,0xDA, 0xED,0xC6, 0x22,0xFF, 0xFD,0x00, 0x00])
  STEP = bytearray([0xF3, 0xCF,0xF3, 0xCF,0x92, 0x49,0xF3, 0xCF,0xF3, 0xCF,0xF3, 0xCF,0xF3, 0xCF,0x00, 0x00])
  SAMPLE = bytearray([0xFF, 0xFF,0xEE, 0xEF,0xE6, 0x67,0xC0, 0x01,0xE6, 0x67,0xEE, 0xEF,0xFF, 0xFF,0x00, 0x00])
  EYE = bytearray([0x3C,0x7E,0xFF,0xE7,0xE7,0xFF,0x7E,0x3C])

  @staticmethod
  def dimensions(icon_data):
    if not icon_data or not isinstance(icon_data, (bytes, bytearray)):
      return 8, 8

    width = len(icon_data)
    height = 8

    max_bit_position = 0
    for byte in icon_data:
      if byte > 0:
        for bit in range(7, -1, -1):
          if byte & (1 << bit):
            max_bit_position = max(max_bit_position, bit)
            break

    if max_bit_position > 0:
      height = max_bit_position + 1
    
    return width, height
  
  @staticmethod
  def is_valid_icon(icon_data):
    return icon_data and isinstance(icon_data, (bytes, bytearray)) and len(icon_data) > 0


class Font:
    alias = {}
    family = {}

    @staticmethod
    def get_font(family='DejaVuSans.ttf', size=50):
        local_font_path = os.path.join(FONTS_DIR, family)
        
        if os.path.exists(local_font_path):
            font_path = local_font_path
        else:
            font_path = family

        if not Font.family.get(font_path):
            Font.family[font_path] = {"sizes": {}}
        if not Font.family[font_path]["sizes"].get(size):
            try:
                Font.family[font_path]["sizes"][size] = ImageFont.truetype(font_path, size=size)
            except OSError:
                # Fallback auf Standard-Font
                print(f"⚠️  Font nicht gefunden: {font_path}, verwende Standard-Font")
                Font.family[font_path]["sizes"][size] = ImageFont.load_default()
                
        return Font.family[font_path]["sizes"].get(size)

    @staticmethod
    def set_alias(alias, family, size):
        Font.alias[alias] = {"family": family, "size": size}

    @staticmethod
    def get(alias):
        font = Font.alias.get(alias)
        if not font:
            return Font.get_font()  # Fallback
            
        family = font.get("family", 'DejaVuSans.ttf')
        size = font.get("size", 50)
        return Font.get_font(family, size)

# Font-Aliases setzen (wie bisher)
Font.set_alias('big', 'DejaVuSans.ttf', 50)
Font.set_alias('normal', 'DejaVuSans.ttf', 20)
Font.set_alias('small', 'DejaVuSans.ttf', 10)
Font.set_alias('digit', 'ds_digital/ds_digi_bold.ttf', 63)

class Align:
  width = 128
  height = 64
  @staticmethod
  def horizontal(align, x, width, total_width=0):
    if not total_width:
      total_width = Align.width
    if align == "left":
      return x
    elif align == "center":
      return x + (total_width - width) // 2
    elif align == "right":
      return x + (total_width - width)
    return x

  @staticmethod
  def vertical(align, y, height, total_height=0):
    if not total_height:
      total_height = Align.height
    if align == "top":
      return y
    elif align == "middle":
      return y + (total_height - height) // 2
    elif align == "bottom":
      return y + (total_height - height)
    return y

  @staticmethod
  def left(x, width, total_width=128):
    return Align.horizontal("left", x, width, total_width)

  @staticmethod
  def center(x, width, total_width=128):
    return Align.horizontal("center", x, width, total_width)

  @staticmethod
  def right(x, width, total_width=128):
    return Align.horizontal("right", x, width, total_width)

  @staticmethod
  def top(y, height, total_height=128):
    return Align.vertical("top", y, height, total_height)

  @staticmethod
  def middle(y, height, total_height=128):
    return Align.vertical("middle", y, height, total_height)

  @staticmethod
  def bottom(y, height, total_height=128):
    return Align.vertical("bottom", y, height, total_height)

class ProgressBar:
  def __init__(self, **data):
    self.x = data.get('x', 0)
    self.y = data.get('y', 0)
    self.value = data.get('value', 0)
    self.min = data.get('min', 0)
    self.max = data.get('max', 100)
    self.width = data.get('width', 100)
    self.height = data.get('height', 10)
    self.fill = data.get('fill', 'white')
    self.background = data.get('background', 'black')
    self.align = data.get('align')
    
  def draw(self, draw):
    try:
      numeric_value = float(self.value)
    except ValueError:
      numeric_value = 0.0

    value_range = self.max - self.min
    if value_range > 0:
      fill_width = int((numeric_value - self.min) / value_range * self.width)
    else:
      fill_width = 0
 
    aligned_x, aligned_y = self.x,self.y
    if self.align:
      aligned_x = Align.horizontal(self.align, self.x, self.width) - 1
    
    draw.rectangle(
      [aligned_x, aligned_y, aligned_x + self.width, aligned_y + self.height],
      outline=self.fill,
      fill=self.background
    )
    
    if fill_width > 0:
      draw.rectangle(
        [aligned_x, aligned_y, aligned_x + fill_width, aligned_y + self.height],
        outline=self.fill,
        fill=self.fill
      )
    return aligned_x, aligned_y


class EyeIcon:
  def __init__(self, min_radius=16, max_radius=32):
      self.min_radius = min_radius
      self.max_radius = max_radius
      self.value = 0  # Linearer Input-Wert vom Regler
      self.size = 100
      
  def update(self, value=None, size=None):
      if not value == None:
          self.value = value  # Linearer Wert z.B. 0-100 vom Encoder
      if not size == None:
          self.size = size

  def _get_transformed_value(self):
    """Transformiert linearen Input in lineare Hin-und-Her-Bewegung zwischen 0-100"""
    # self.value ist linear (z.B. 0-100 vom Encoder)
    
    # Berechne Position im Zyklus (0-200)
    cycle_position = self.value % 200
    
    # Transformiere auf 0-100 Hin- und 100-0 Zurück
    if cycle_position <= 100:
        # Hin: 0 → 100
        transformed = cycle_position
    else:
        # Zurück: 100 → 0  
        transformed = 200 - cycle_position
    
    return transformed

  def draw(self, draw, item, x, y, grow=100):
    ratio = min(self.size / grow, 1.0)
    eye_radius = int(self.min_radius + (self.max_radius - self.min_radius) * ratio)
    
    eye_x = x + eye_radius
    eye_y = 80 - eye_radius * 2

    # 1. Auge zeichnen (weiß)
    draw.ellipse([
        eye_x - eye_radius, eye_y - eye_radius,
        eye_x + eye_radius, eye_y + eye_radius
    ], outline="white", fill="white")

    # 2. Pupille zeichnen (schwarz)
    pupil_sin = math.sin(math.radians(self.value))
    pupil_radius = int(eye_radius * 0.3)
    pupil_size = pupil_radius - abs(int(pupil_sin * pupil_radius))
    pupil_x = pupil_sin * eye_radius
    #print(f"EYE: {eye_radius} PUPIL: {pupil_sin} RAD: {pupil_radius} SIZE: {pupil_size}")
    draw.ellipse([
        pupil_x + eye_x - pupil_size, eye_y - pupil_size,
        pupil_x + eye_x + pupil_size, eye_y + pupil_size
    ], outline="black", fill="black")

    # 3. LIDER ZULETZT zeichnen (grau) - JETZT ÜBER DER PUPILLE!
    transformed_value = self._get_transformed_value()
    self._draw_eyelid_inset(draw, eye_x, eye_y, eye_radius, transformed_value)

    # 4. Auge-Umriss nochmal darüber (optional)
    draw.ellipse([
        eye_x - eye_radius, eye_y - eye_radius,
        eye_x + eye_radius, eye_y + eye_radius
    ], outline="white")
  
  def _draw_eyelid_inset(self, draw, center_x, center_y, eye_radius, value):
    coverage_ratio = (value / 100.0) * 0.5
    lid_height_total = eye_radius * 2 * coverage_ratio
    
    if lid_height_total >= 0.1:
        lid_top = center_y - eye_radius
        lid_bottom_upper = lid_top + lid_height_total
        lid_bottom_lower = center_y + eye_radius - lid_height_total
        
        # Zeichne viele kleine Linien statt großer Flächen
        for x in range(center_x - eye_radius, center_x + eye_radius + 1):
            dx = x - center_x
            if abs(dx) <= eye_radius:
                radicand = max(0, eye_radius**2 - dx**2)
                circle_y_upper = center_y - math.sqrt(radicand)
                circle_y_lower = center_y + math.sqrt(radicand)
                
                # OBERES LID: Vertikale Linie von Kreisrand bis Lid-Linie
                if lid_bottom_upper > circle_y_upper:
                    draw.line([
                        x, int(circle_y_upper),
                        x, int(lid_bottom_upper)
                    ], fill="#808080", width=1)
                
                # UNTERES LID: Vertikale Linie von Kreisrand bis Lid-Linie
                if lid_bottom_lower < circle_y_lower:
                    draw.line([
                        x, int(lid_bottom_lower), 
                        x, int(circle_y_lower)
                    ], fill="#808080", width=1)     
        

class Layout:
  def __init__(self, name, dither=False):
    self.name = name
    self.has_changed = False
    self.items = []
    self.overlays = {}
    self.items_hash = ""
    self.overlays_hash = ""
    self.dither = dither
    self.latecomer = 0
    Font.set_alias('big', 'DejaVuSans.ttf', 50)
    Font.set_alias('normal', 'DejaVuSans.ttf', 20)
    Font.set_alias('small', 'DejaVuSans.ttf', 10)
    Font.set_alias('digit', 'assets/fonts/ds_digital/ds_digi_bold.ttf', 63)

  def update(self, items):
    #print(f"🎯 LAYOUT.update called with {len(items)} items: {items}")
    items_hash = hash(str(items))
    
    if self.items_hash == items_hash:
        self.has_changed = False
        #print("⏭️  No changes detected")
    else:
        self.items = items
        self.has_changed = True
        self.latecomer = 2
        self.items_hash = items_hash
        #print("✅ Changes detected - will redraw")

  def overlay(self, overlay):
    if uid := overlay["uid"]:
      self.overlays[uid] = overlay
      overlays_hash = hash(str(self.overlays))

      if self.overlays_hash == overlays_hash:
        self.has_changed = False
      else:
        self.has_changed = True
        self.latecomer = 2
        self.overlays_hash = overlays_hash

  def draw_element(self, draw, item, x, y):
    if element := item.get("element"):
      if item.get("x"):
        x += item.get("x")
      if item.get("y"):
        y += item.get("y")
      element.draw(draw, item, x, y)
    return x, y

  def draw_icon(self, draw, item, x, y):
    if icon := item.get("icon"):
      width, height = Icon.dimensions(icon)
      if item.get("x"):
        x += item.get("x")
      if item.get("y"):
        y += item.get("y")
      align = item.get("align")
      if align == "right":
        if text := item.get("text"):
          font = item.get("font", Font.get("big"))
          text_width = draw.textlength(text, font=font)
          x = Align.horizontal(align, x, width) - text_width - item.get("space", 0)
        else:
          x = Align.horizontal(align, x, width)
      if align == "center":
        if text := item.get("text"):
          font = item.get("font", Font.get("big"))
          text_width = draw.textlength(text, font=font)
          full_width = (text_width + width + item.get("space", 0))
          x = x + (Align.width - full_width) // 2
        else:
          x = Align.horizontal(align, x, width)
      if valign := item.get("valign"):
        y = Align.vertical(valign, y, height)
        #print(f"Y: {y} HEIGHT: {height} MODE: {valign}")
      draw.bitmap((x, y), Image.frombytes('1', (width, height), bytes(icon)), fill=item.get("fill", "white"))
      if not (align == "right"):
        if width:
          x += width
        if space := item.get("space"):
          x += space
      else:
        x = 0
    return x, y

  def draw_text(self, draw, item, x, y):
    if not item.get("text") == None:
      text = f"{item.get('text')}"
      font = item.get("font", Font.get("big"))
      ascent, descent = font.getmetrics()
      width = draw.textlength(text, font=font)
      height = ascent + descent
      if item.get("x"):
        x += item.get("x")
      if item.get("y"):
        y += item.get("y")
      y -= descent
      if align := item.get("align"):
        if not item.get("icon"):
          x = Align.horizontal(align, x, width)
        else:
          if not (align == "center"):
            x = Align.horizontal(align, x, width)
      if not item.get("icon"):
        if valign := item.get("valign"):
          #print(f"TEXT ALIGN: {valign} Y:{y} HEIGHT: {height}")
          y = Align.vertical(valign, 0, height)

      draw.text((x, y), f"{text}", fill=item.get("fill", "white"), font=font)
    return x, y

  def draw_progress_bar(self, draw, item, x, y):
    if data := item.get("progressbar"):
      bar = ProgressBar(**data)
      bar.draw(draw)
    return x, y

  def layout(self, draw, items=[]):
    for item in items:
      self.set_dither((item.get("dither") == True))
      x = 0
      y = 0
      x, y = self.draw_icon(draw, item, x, y)
      x, y = self.draw_text(draw, item, x, y)
      x, y = self.draw_progress_bar(draw, item, x, y)
      x, y = self.draw_element(draw, item, x, y)

  def layout_overlay(self, draw, items={}):
    for key in items:
      item = items[key]
      x = 0
      y = 0
      x, y = self.draw_icon(draw, item, x, y)
      x, y = self.draw_text(draw, item, x, y)
      x, y = self.draw_progress_bar(draw, item, x, y)
      x, y = self.draw_element(draw, item, x, y)

  def paint(self, draw):
    if len(self.items):
      self.layout(draw, self.items)
    if len(self.overlays):
      self.layout_overlay(draw, self.overlays)

  def get_dither(self):
    for item in self.items:
      if item.get("dither") == True:
        return True
    for item in self.overlays:
      if item.get("dither") == True:
        return True
    return False
      

  def set_dither(self, state):
    self.dither = state

  def uni(self):
    self.dither = False

  def gray(self):
    self.dither = True

  def draw(self, device=None):
    if self.has_changed and device:
      with canvas(device, dither=self.get_dither()) as draw:
        self.paint(draw)
      self.has_changed = False
      self.latecomer -= 1

