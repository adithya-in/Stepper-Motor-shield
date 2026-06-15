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

# Check what's in each column for the reversed grid
print("Content at each column for all rows (reversed):")
print("Format: row6(y=max) ... row0(y=0)")
print()

# Find columns that are empty in ALL rows
all_empty = []
for col in range(max_x + 1):
    empty = all(row[col] == " " for row in grid)
    all_empty.append(empty)

# Print the grid reversed with column numbers
reversed_grid = list(reversed(grid))

# Find letter clusters in reversed grid (groups of consecutive non-all-empty columns)
# with at least 2 consecutive all-empty as separator
clusters = []
in_cluster = False
cluster_start = 0
for col in range(max_x + 1):
    if not all_empty[col]:
        if not in_cluster:
            cluster_start = col
            in_cluster = True
    else:
        if in_cluster:
            # Check if this is part of a multi-space separator
            # Look ahead for 2+ consecutive empty cols
            gap_end = col
            while gap_end <= max_x and all_empty[gap_end]:
                gap_end += 1
            gap_len = gap_end - col
            if gap_len >= 2:
                clusters.append((cluster_start, col - 1))
                in_cluster = False
            # else: single empty column within a cluster, keep going
if in_cluster:
    clusters.append((cluster_start, max_x))

print(f"Found {len(clusters)} letter clusters:")
for i, (start, end) in enumerate(clusters):
    print(f"\nLetter {i+1} (cols {start}-{end}):")
    for row in reversed_grid:
        line = "".join(row[start:end+1])
        print(f"  {line}")
    
    # Also print just █ characters for shape analysis
    print(f"  Shape (█ only):")
    for row in reversed_grid:
        line = "".join(row[start:end+1])
        shape = "".join("#" if c == "#" else " " for c in line)
        print(f"  {shape}")
