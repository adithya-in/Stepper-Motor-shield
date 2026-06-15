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
    grid[y][x] = "#" if char == "\u2588" else ("." if char == "\u2591" else char)

# Detect letter groups by looking for columns that are all spaces
non_empty_cols = set()
for row in grid:
    for col, ch in enumerate(row):
        if ch != " ":
            non_empty_cols.add(col)

# Find contiguous ranges of non-empty columns
sorted_cols = sorted(non_empty_cols)
groups = []
current_start = sorted_cols[0] if sorted_cols else None
current_end = sorted_cols[0] if sorted_cols else None

for col in sorted_cols[1:]:
    if col == current_end + 1:
        current_end = col
    else:
        groups.append((current_start, current_end))
        current_start = col
        current_end = col
if current_start is not None:
    groups.append((current_start, current_end))

# Print each letter group
print("Letter groups found:")
for i, (start, end) in enumerate(groups):
    print(f"\nLetter {i+1} (cols {start}-{end}):")
    # Use reversed grid for correct orientation
    for row in reversed(grid):
        print("".join(row[start:end+1]))
