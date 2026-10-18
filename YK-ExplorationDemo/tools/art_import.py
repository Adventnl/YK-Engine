"""Deterministic extraction of complete objects from generated sprite masters."""

def silhouette(image):
    bounds = image.getchannel('A').point(lambda a: 255 if a >= 80 else 0).getbbox()
    if not bounds:
        raise ValueError('Empty generated object')
    return image.crop(bounds)

def generated_objects(sheet, columns, rows, alpha_threshold=80):
    """Find complete objects in the generated sheet's transparent gutters.

    Generators don't always honor mathematical cell boundaries. Empty alpha bands
    define the actual rows/columns, preventing clipped hair, banners and furniture.
    """
    alpha = sheet.getchannel('A').point(lambda a: 255 if a >= alpha_threshold else 0)
    def bands(values):
        found, start = [], None
        for i, occupied in enumerate(values + [False]):
            if occupied and start is None:
                start = i
            elif not occupied and start is not None:
                found.append((start, i))
                start = None
        return found
    row_bands = bands([alpha.crop((0, y, sheet.width, y + 1)).getbbox() is not None for y in range(sheet.height)])
    if len(row_bands) != rows:
        raise ValueError(f'Expected {rows} complete generated rows, found {row_bands}')
    result = []
    for top, bottom in row_bands:
        row_alpha = alpha.crop((0, top, sheet.width, bottom))
        columns_found = bands([row_alpha.crop((x, 0, x + 1, bottom - top)).getbbox() is not None for x in range(sheet.width)])
        if len(columns_found) != columns:
            raise ValueError(f'Expected {columns} complete generated columns, found {columns_found}')
        for left, right in columns_found:
            result.append(sheet.crop((max(0, left - 1), max(0, top - 1), min(sheet.width, right + 1), min(sheet.height, bottom + 1))))
    return result

