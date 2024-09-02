import os.path
from PIL import Image, ImageDraw, ImageFont

def textsize(text, font):
    im = Image.new(mode="P", size=(0, 0))
    draw = ImageDraw.Draw(im)
    _, _, width, height = draw.textbbox((0, 0), text=text, font=font)
    return width, height

def create_image(number, resolution, label, suffix, output_dir):
    width, height = resolution
    image = Image.new('RGB', (width, height), color=(255, 255, 255))
    draw = ImageDraw.Draw(image)

    try:
        font = ImageFont.truetype("arial.ttf", int(height / 3))
    except IOError:
        font = ImageFont.load_default(size=int(height / 3))

    text = str(number) + f"\n{label} Image"
    text_width, text_height = textsize(text, font=font)
    text_x = (width - text_width) // 2
    text_y = (height - text_height) // 2
    draw.text((text_x, text_y), text, fill=(0, 0, 0), font=font)

    filename = os.path.join(output_dir, f"Image_{number}_{suffix}.png")
    image.save(filename)

def generate_images(start, end, output_dir='romfs/screenshots'):
    for number in range(start, end + 1):
        create_image(number, (854, 480), "DRC", "DRC", output_dir)
        create_image(number, (960, 720), "TV", "TV", output_dir)

if __name__ == "__main__":
    generate_images(1, 20)
