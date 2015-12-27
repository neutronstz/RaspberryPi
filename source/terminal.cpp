#include "terminal.h"
#include "buffer.h"
#include "control_list.h"
#include "io.h"
#include "uart.h"
#include "terminal_data.h"

Terminal Terminal::init(const Framebuffer &framebuffer)
{
    Terminal result;
    
    uart_puts("terminal init\r\n");
    
    result._framebuffer = framebuffer;
    return result;
}

struct TerminalUniforms
{
    size_t terminal_width;              // in characters
    const Character *terminal_data;     //
    const uint32_t *palette_data_fg;    // foreground palette colors, 256 max
    const uint32_t *palette_data_bg;    // background palette colors, 256 max
    const uint8_t *font_data;           // pointer to 8x12 RGBA font data, 256 characters, 16 per row
};

void Terminal::render(const Character *buffer) const
{
    uint8_t *bin_addr = (uint8_t*)0x400000;
    uint8_t *bin_base = (uint8_t*)0x500000;
    
    // size of framebuffer in tiles
    size_t tx = (_framebuffer.width() + 63) / 64; // TODO: what controls the tile size?
    size_t ty = (_framebuffer.height() + 63) / 64;
    
    // size of terminal in characters
    size_t cx = _framebuffer.width() / 8;
    
    alignas(16) TerminalUniforms uniforms;
    uniforms.terminal_width = cx;
    uniforms.terminal_data = buffer;
    uniforms.palette_data_fg = &palette[0];
    uniforms.palette_data_bg = &palette[0];
    uniforms.font_data = &terminal_font.data[0];
    
    // shader state
    alignas(16) NVShaderState shaderState;
    shaderState.fragment_shader = &shader_code;
    shaderState.uniform_count = 5;
    shaderState.fragment_shader_uniforms = &uniforms;
    shaderState.vertex_stride = 12;
    shaderState.vertex_data = &vertices;
    
    // binning control list
    WriteBuffer<128, 16> binBuf;
    binBuf.write(ControlTileBinningConfig(bin_addr, 0x2000, bin_base, tx, ty, ControlTileBinningConfigFlags::auto_init_state));
    binBuf.write(ControlStartTileBinning());
    binBuf.write(ControlClipWindow(0, 0, _framebuffer.width(), _framebuffer.height()));
    binBuf.write(ControlConfigBits( (ControlConfigBitsFlags)((size_t)ControlConfigBitsFlags::forward_facing | (size_t)ControlConfigBitsFlags::reverse_facing | (size_t)ControlConfigBitsFlags::early_z_updates) ));
    binBuf.write(ControlViewportOffset(0, 0));
    binBuf.write(ControlNVShaderState(&shaderState));
    binBuf.write(ControlVertexArrayPrimitives(PrimitiveMode::triangle_strip, 0, 4));
    binBuf.write(ControlFlushAllState());
    //binBuf.write(ControlFlush());
    
    if (binBuf.fail())
    {
        uart_puts("binning buffer too small\r\n");
        return;
    }
    
    // rendering control list
    WriteBuffer<32 * 1024, 16> renBuf;
    renBuf.write(ControlClearColors(0xFF00FFFF));
    renBuf.write(ControlTileRenderingConfig(_framebuffer, ControlTileRenderingConfigFlags::color_format_rgba8888));
    renBuf.write(ControlTileCoordinates(0, 0));
    renBuf.write(ControlStoreTileBuffer()); // TODO: params
    
    for (size_t y = 0; y < ty; y++)
    {
        for (size_t x = 0; x < tx; x++)
        {
            renBuf.write(ControlTileCoordinates(x, y));
            renBuf.write(ControlBranchToSublist(bin_addr + ((y * tx + x) * 32)));
            
            if (x == tx - 1 && y == ty - 1)
                renBuf.write(ControlStoreMultisampleEnd());
            else
                renBuf.write(ControlStoreMultisample());
        }
    }
    
    if (renBuf.fail())
    {
        uart_puts("render buffer too small\r\n");
        return;
    }
    
    size_t binAddr = (size_t)binBuf.data();
    
    mmio_write(V3D_CT0CS, 0x20);                    // request thread stop
    while (mmio_read(V3D_CT0CS) & 0x20) { }         // wait for it to stop
    mmio_write(V3D_BFC, 1);                         // reset binning frame count
    
    mmio_write(V3D_CT0CA, binAddr);                 // set current address
    mmio_write(V3D_CT0EA, binAddr + binBuf.size()); // set end address, automatically starts
    
    while (mmio_read(V3D_BFC) == 0) { }             // wait for it to finish
    
    /****************************************/
    
    size_t renAddr = (size_t)renBuf.data();
    
    mmio_write(V3D_CT1CS, 0x20);                    // request thread stop
    while (mmio_read(V3D_CT1CS) & 0x20) { }         // wait for it to stop
    mmio_write(V3D_RFC, 1);                         // reset rendering frame count
    
    mmio_write(V3D_CT1CA, renAddr);                 // set current address
    mmio_write(V3D_CT1EA, renAddr + renBuf.size()); // set end address, automatically starts
    
    while (mmio_read(V3D_RFC) == 0) { }             // wait for it to finish
    
    uart_puts("done!\r\n");
    
    /*uart_printf("\r\n");
    uart_printf("V3D_ERRSTAT: 0x%.8x\r\n", mmio_read(V3D_ERRSTAT));
    uart_printf("\r\n");
    uart_printf("V3D_INTCTL: %.8X\r\n", mmio_read(V3D_INTCTL));
    uart_printf("V3D_INTENA: %.8X\r\n", mmio_read(V3D_INTENA));
    uart_printf("\r\n");
    uart_printf("V3D_DBGE:  0x%.8x\r\n", mmio_read(V3D_DBGE));
    uart_printf("V3D_FDBGO: 0x%.8x\r\n", mmio_read(V3D_FDBGO));
    uart_printf("V3D_FDBGB: 0x%.8x\r\n", mmio_read(V3D_FDBGR));
    uart_printf("V3D_FDBGR: 0x%.8x\r\n", mmio_read(V3D_FDBGB));
    uart_printf("V3D_FDBGS: 0x%.8x\r\n", mmio_read(V3D_FDBGS));
    uart_printf("\r\n");*/
}