`ORG.IPEP:`
![GitHub release (latest by date)](https://img.shields.io/github/v/release/ImageProcessing-ElectronicPublications/jpegm)
![GitHub Release Date](https://img.shields.io/github/release-date/ImageProcessing-ElectronicPublications/jpegm)
![GitHub repo size](https://img.shields.io/github/repo-size/ImageProcessing-ElectronicPublications/jpegm)
![GitHub all releases](https://img.shields.io/github/downloads/ImageProcessing-ElectronicPublications/jpegm/total)
![GitHub](https://img.shields.io/github/license/ImageProcessing-ElectronicPublications/jpegm)  

# JPEGM

## Minimalist JPEG encoder & decoder

### Encoder (`jpegmenc`)

- written in C99
- supports subsampled components (4:4:4, 4:2:2, 4:2:0)
- uses interleaved scan
- supports quality setting (1..100)
- support color and grayscale images
- uses default Huffman table or optimized tables
- can handle 8-bit and 12-bit input images

### Decoder (`jpegmdec`)

- written in C99
- can parse YCbCr, YCCK and grayscale images
- can handle 8-bit or 12-bit samples
- can handle subsampled components
- can handle restart markers
- supports interleaved and non-interleaved scans
- supports Motion JPEG
- does not support progressive JPEG files
- does not support arithmetic coding

## Author

David Barina <ibarina@fit.vutbr.cz>

## License

This project is licensed under the MIT License.
