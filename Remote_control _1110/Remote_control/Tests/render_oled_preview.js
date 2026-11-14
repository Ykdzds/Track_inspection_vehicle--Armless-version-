const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');

const [input, output] = process.argv.slice(2);
if (!input || !output) throw new Error('需要指定测试快照文本和PNG输出路径');
const rows = fs.readFileSync(input, 'utf8').trimEnd().split(/\r?\n/);
const pages = [];
for (let cursor = 0; cursor < rows.length;) {
    const heading = rows[cursor++];
    if (!heading.startsWith('PAGE ')) throw new Error('Invalid framebuffer header');
    const data = Buffer.from(rows.slice(cursor, cursor + 8).join(''), 'hex');
    cursor += 8;
    if (data.length !== 1024) throw new Error('Invalid framebuffer length');
    pages.push({label: heading.slice(5), data});
}
const scale = 3;
const columns = 3;
const cellWidth = 144;
const cellHeight = 88;
const width = columns * cellWidth * scale;
const height = Math.ceil(pages.length / columns) * cellHeight * scale;
const pixels = Buffer.alloc(width * height * 3, 16);
function pixel(horizontal, vertical, color) {
    for (let subrow = 0; subrow < scale; subrow++) {
        for (let subcolumn = 0; subcolumn < scale; subcolumn++) {
            const offset = ((vertical * scale + subrow) * width + horizontal * scale + subcolumn) * 3;
            pixels[offset] = color[0]; pixels[offset + 1] = color[1]; pixels[offset + 2] = color[2];
        }
    }
}
pages.forEach((page, index) => {
    const left = (index % columns) * cellWidth + 8;
    const top = Math.floor(index / columns) * cellHeight + 4;
    for (let row = 0; row < 64; row++) {
        for (let column = 0; column < 128; column++) {
            pixel(left + column, top + 12 + row,
                page.data[(row >> 3) * 128 + column] & (1 << (row & 7)) ? [80, 225, 255] : [0, 0, 0]);
        }
    }
});
function crc32(buffer) {
    let crc = 0xffffffff;
    for (const byte of buffer) {
        crc ^= byte;
        for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    return (crc ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
    const payload = Buffer.concat([Buffer.from(type), data]);
    const size = Buffer.alloc(4); size.writeUInt32BE(data.length);
    const checksum = Buffer.alloc(4); checksum.writeUInt32BE(crc32(payload));
    return Buffer.concat([size, payload, checksum]);
}
const header = Buffer.alloc(13);
header.writeUInt32BE(width, 0); header.writeUInt32BE(height, 4); header[8] = 8; header[9] = 2;
const scanlines = Buffer.alloc((width * 3 + 1) * height);
for (let row = 0; row < height; row++) pixels.copy(scanlines, row * (width * 3 + 1) + 1, row * width * 3, (row + 1) * width * 3);
fs.writeFileSync(output, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', header), chunk('IDAT', zlib.deflateSync(scanlines)), chunk('IEND', Buffer.alloc(0))]));
console.log('已用工程真实显存渲染' + pages.length + '个场景：' + output);
