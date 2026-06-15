import urllib.request
import re

def decode_secret_message(url):
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

    # Determine which orientation: check if row 0 has characters on the left
    # (if so, y=0 is likely the top and we don't reverse)
    leftmost_occupied = {}
    for y_idx in range(max_y + 1):
        for x_idx in range(max_x + 1):
            if grid[y_idx][x_idx] != " ":
                leftmost_occupied[y_idx] = x_idx
                break

    print("Leftmost occupied column per row (original):")
    for y_idx in sorted(leftmost_occupied.keys()):
        print(f"  y={y_idx}: col {leftmost_occupied[y_idx]}")

    # Print both orientations
    print("\n=== Orientation A: y=0 at TOP, y=max_y at BOTTOM ===")
    for row in grid:
        print("".join(row))

    print("\n=== Orientation B: y=max_y at TOP, y=0 at BOTTOM (reversed) ===")
    for row in reversed(grid):
        print("".join(row))


if __name__ == "__main__":
    url = "https://docs.google.com/document/d/e/2PACX-1vSvM5gDlNvt7npYHhp_XfsJvuntUhq184By5xO_pA4b_gCWeXb6dM6ZxwN8rE6S4ghUsCj2VKR21oEP/pub"
    decode_secret_message(url)
