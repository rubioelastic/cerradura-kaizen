



void M5PrintCenter(auto &device, int x, int y, int w, int h,
                   uint8_t dimFontMax, uint16_t fontColor,
                   uint16_t bgColor, const char* texto)
{
    if (!texto || texto[0] == '\0') {
        device.fillRect(x, y, w, h, bgColor);
        return;
    }

    size_t lengthTexto = strlen(texto);

    // ======================================================================
    // 1) TABLA REAL que ya usas tú (sin inventos)
    // ======================================================================
    auto recalcularMedidas = [&](uint8_t d,
                                 uint8_t &col, uint8_t &sep, uint8_t &filas)
    {
        if (d == 1) { col=5;  sep=1;  filas=7;  }
        if (d == 2) { col=10; sep=2;  filas=16; }
        if (d == 3) { col=15; sep=3;  filas=24; }
        if (d == 4) { col=21; sep=3;  filas=28; }
        if (d == 5) { col=25; sep=5;  filas=35; }
    };

    auto anchoNecesario = [&](uint8_t d, size_t n)
    {
        if (n == 0) return 0;
        uint8_t col, sep, filas;
        recalcularMedidas(d, col, sep, filas);
        return (int)(n * col + (n - 1) * sep);
    };

    // ======================================================================
    // 2) SELECCIÓN REAL DEL TAMAÑO DE LETRA:
    //    del máximo → mínimo, si CABE EN EL ANCHO (w)
    // ======================================================================
    uint8_t dimLetra = 1;
    for (int size = dimFontMax; size >= 1; size--) {
        if (anchoNecesario(size, lengthTexto) <= w) {
            dimLetra = size;
            break;
        }
    }

    // ======================================================================
    // 3) Cálculo real de medidas con el tamaño elegido
    // ======================================================================
    uint8_t columnasPorLetra, columnasEntreLetras, filas;
    recalcularMedidas(dimLetra, columnasPorLetra, columnasEntreLetras, filas);

    // ======================================================================
    // 4) Ajustar tamaño si no cabe en ALTO (h)
    // ======================================================================
    while (filas > h && dimLetra > 1) {
        dimLetra--;
        recalcularMedidas(dimLetra, columnasPorLetra, columnasEntreLetras, filas);
    }

    // ======================================================================
    // 5) Calcular cuántos caracteres caben con este tamaño
    // ======================================================================
    auto anchoConDim = [&](size_t n) {
        if (n == 0) return 0;
        return (int)(n * columnasPorLetra + (n - 1) * columnasEntreLetras);
    };

    size_t maxChars = lengthTexto;
    while (maxChars > 0 && anchoConDim(maxChars) > w) {
        maxChars--;
    }

    if (maxChars == 0) {
        device.fillRect(x, y, w, h, bgColor);
        return;
    }

    // ======================================================================
    // 6) Copia EXACTA Latin-1/CP1252 (tus chars reales tipo \244)
    // ======================================================================
    static char buffer[256];
    if (maxChars > sizeof(buffer)-1) maxChars = sizeof(buffer)-1;
    memcpy(buffer, texto, maxChars);
    buffer[maxChars] = '\0';

    // ======================================================================
    // 7) Centrados
    // ======================================================================
    int textWidth = anchoConDim(maxChars);
    int drawX = x + (w - textWidth) / 2;
    int drawY = y + (h - filas) / 2;

    if (drawX < x) drawX = x;
    if (drawY < y) drawY = y;

    // ======================================================================
    // 8) Pintar
    // ======================================================================
    device.fillRect(x, y, w, h, bgColor);

    device.setTextSize(dimLetra);
    device.setTextColor(fontColor, bgColor);
    device.setCursor(drawX, drawY);
    device.printf("%s", buffer);
}




void M5PrintCenterF(auto &device,
                    int x, int y, int w, int h,
                    uint8_t dimFontMax, uint16_t fontColor, uint16_t bgColor,
                    const char *fmt, ...)
{
    char buf[128];                // sube si necesitas más
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n <= 0) return;
    buf[sizeof(buf)-1] = '\0';    // por si acaso

    M5PrintCenter(device, x, y, w, h, dimFontMax, fontColor, bgColor, buf);
}