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

# Print the full reversed grid
print("Full grid (reversed):")
for row in reversed(grid):
    print("".join(row))

print()

# For each column, print whether it's empty in all rows
non_empty_cols = set()
for col in range(max_x + 1):
    for row in grid:
        if row[col] != " ":
            non_empty_cols.add(col)
            break

# Find letter group boundaries
sorted_cols = sorted(non_empty_cols)
groups = []
current_start = sorted_cols[0]
current_end = sorted_cols[0]
for col in sorted_cols[1:]:
    if col == current_end + 1:
        current_end = col
    else:
        groups.append((current_start, current_end))
        current_start = col
        current_end = col
groups.append((current_start, current_end))

print(f"Number of letter groups detected: {len(groups)}")
print()

# Re-detect groups but with a gap threshold of 2
# (if there are 2+ consecutive empty columns, split)
all_empty_cols = []
for col in range(max_x + 1):
    all_empty = all(row[col] == " " for row in grid)
    all_empty_cols.append(all_empty)

# Find gaps of 2+ consecutive empty columns
gaps = []
in_gap = False
gap_start = 0
for col in range(max_x + 1):
    if all_empty_cols[col]:
        if not in_gap:
            gap_start = col
            in_gap = True
    else:
        if in_gap:
            if col - gap_start >= 2:
                gaps.append((gap_start, col - 1))
            in_gap = False
if in_gap and max_x + 1 - gap_start >= 2:
    gaps.append((gap_start, max_x))

print("Gaps (2+ consecutive empty columns):")
for g in gaps:
    print(f"  cols {g[0]}-{g[1]}")

# Now split by gaps
letter_ranges = []
prev_end = -1
for gap_start, gap_end in gaps:
    if gap_start > prev_end + 1:
        letter_ranges.append((prev_end + 1, gap_start - 1))
    prev_end = gap_end
if prev_end < max_x:
    letter_ranges.append((prev_end + 1, max_x))

print(f"\nLetter ranges (split by gaps):")
for i, (start, end) in enumerate(letter_ranges):
    print(f"\nLetter {i+1} (cols {start}-{end}):")
    for row in reversed(grid):
        print(f"  {''.join(row[start:end+1])}")
