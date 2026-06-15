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

grid = [[" " for _ in range(max_x + 1)] for _ in range(max_y + 1)]
for x, y, char in points:
    grid[y][x] = char

# Based on row 0 analysis, the 8 letter column ranges are:
letter_ranges = [
    (0, 10),   # Letter 1: H
    (13, 20),  # Letter 2
    (23, 33),  # Letter 3
    (34, 43),  # Letter 4
    (44, 50),  # Letter 5
    (52, 60),  # Letter 6
    (62, 70),  # Letter 7
    (73, 81),  # Letter 8
]

for i, (start, end) in enumerate(letter_ranges):
    print(f"Letter {i+1} (cols {start}-{end}):")
    # Print with full Unicode characters, reversed (y=max_y at top)
    for row in reversed(grid):
        line = "".join(row[start:end+1])
        print(f"  {line}")
    print()
