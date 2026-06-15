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

# Print full grid reversed
print("Full grid (y=max_y at top):")
for row in reversed(grid):
    print("".join(row))

print()

# Now split by finding columns that are EMPTY in ALL rows
# Width threshold: 2+ consecutive empty columns = letter boundary
empty_cols = []
for col in range(max_x + 1):
    all_empty = all(row[col] == " " for row in grid)
    empty_cols.append(all_empty)

# Find boundaries: start at 0, then each time we see 2+ consecutive empty cols, split
# Actually, any single column that's empty in all rows is a boundary
boundaries = [0]
for col in range(1, max_x + 1):
    if empty_cols[col]:
        boundaries.append(col)

# Groups are between boundaries
# But a boundary might just be 1 column, merge if not 2+ consecutive
letter_bounds = []
prev = 0
for b in boundaries[1:]:
    # If gap is 2+, it's a separator
    if b - prev >= 2:
        pass  # gap is the separator
    if prev >= 0:
        # Find the actual letter column range
        pass

# Simpler: just print using the 7-group ranges from earlier
letter_ranges_7 = [
    (0, 10),    # H
    (12, 23),   # ?
    (25, 42),   # ? (wide)
    (44, 50),   # I
    (52, 63),   # O/D
    (65, 75),   # B 
    (77, 89),   # D
]

print("=== 7 Groups (determined by column emptiness) ===")
for i, (start, end) in enumerate(letter_ranges_7):
    print(f"Letter {i+1} (cols {start}-{end}):")
    for row in reversed(grid):
        print("  " + "".join(row[start:end+1]))
    print()
