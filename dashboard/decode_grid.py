import urllib.request
import re

url = "https://docs.google.com/document/d/e/2PACX-1vSvM5gDlNvt7npYHhp_XfsJvuntUhq184By5xO_pA4b_gCWeXb6dM6ZxwN8rE6S4ghUsCj2VKR21oEP/pub"
html = urllib.request.urlopen(url).read().decode("utf-8")
table_match = re.search(r"<table[^>]*>(.*?)</table>", html, re.DOTALL)
table_html = table_match.group(1)
rows = re.findall(r"<tr[^>]*>(.*?)</tr>", table_html, re.DOTALL)

points = []
for row in rows:
    cells = re.findall(r"<td[^>]*>(.*?)</td>", row, re.DOTALL)
    if len(cells) == 3:
        clean = [re.sub(r"<[^>]+>", "", c).strip() for c in cells]
        try:
            x = int(clean[0])
            y = int(clean[2])
            char = clean[1]
            points.append((x, y, char))
        except:
            pass

max_x = max(p[0] for p in points)
max_y = max(p[1] for p in points)
print(f"Grid size: {max_x + 1} x {max_y + 1}")
print(f"Total points: {len(points)}")

# Sort by y then x
points.sort(key=lambda p: (p[1], p[0]))

grid = [[" " for _ in range(max_x + 1)] for _ in range(max_y + 1)]
for x, y, char in points:
    grid[y][x] = "#" if char == "\u2588" else ("." if char == "\u2591" else char)

print()
print("Grid (reversed - max_y to 0):")
for row in reversed(grid):
    print("".join(row))

print()
# Print column markers every 10 columns
header = ""
for i in range(max_x + 1):
    if i % 10 == 0:
        header += str(i // 10)
    else:
        header += " "
print("Columns:")
print(header)
print("0123456789" * 9)

print()
# Print each row with its y-coordinate
print("Rows (with y-coordinates):")
for y_idx, row in enumerate(grid):
    print(f"y={y_idx}: " + "".join(row))
