#include <cassert>
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <string>
#include <iterator>
#include <sstream>
#include <alsa/asoundlib.h>
#include <png.h>
#include <cmath>

using hertz = double;
using raw_buffer = std::vector<uint8_t>;
using audio_buffer = std::vector<uint32_t>; 

struct audio_format
{
};

struct wave_format : public audio_format
{
	std::string filename;
	struct format_header { 
		std::string	  magick;
		uint32_t	  size;
		std::string	  type;
		std::string	  format_chunk_marker;	// fmt+null
		uint32_t	  subchunk_size;
		uint16_t	  format_type;
		uint16_t	  num_channels;
		uint32_t	  sample_rate;			// sample_rate == number of samples per second == hertz
		uint32_t	  byte_rate;				// ( sample_rate * bits_per_sample * channels ) / 8
		uint16_t	  block_align;				// ( bits per sample * channels ) / 8.1
		uint16_t	  bits_per_sample;	
		std::string	  data_chunk_header;	// data (no null)
		uint32_t	  data_size;				// length of data in bytes
	};

	format_header header;
	raw_buffer buffer;				// contains entire wave data plus header
	size_t data_begin_index;		// where the data starts
	uint32_t duration;
	uint32_t num_samples;
	std::vector<audio_buffer> channels;
};

uint16_t read16(std::vector<uint8_t> const & buffer, size_t& cursor ) 
{
	uint16_t value=0;
	for( size_t ii=0; ii < 2; ii++ ) {
		value |= buffer[ cursor++ ] << (8*ii);
	}
	return value;
}

uint32_t read32(std::vector<uint8_t> const & buffer, size_t& cursor ) 
{
	uint32_t value = 0;
	for( size_t ii=0; ii < 4; ii++ ) {
		value |= buffer[ cursor++ ] << (8*ii);
	}
	return value;
}

std::string read_string(std::vector<uint8_t> const& buffer, size_t& cursor, size_t count )
{
	std::stringstream ss;
	for( size_t ii=0; ii < count; ii++ ) {
		ss << buffer[cursor++];
	}
	ss << '\0';
	return ss.str();
}

void readN( std::vector<uint8_t> const & buffer, size_t& cursor, size_t count, std::vector<uint32_t>& track )
{
	while( count > 0 ) {
		uint32_t value = 0;
		for( size_t ii=0; ii < 4; ii++ ) {
			if ( cursor < buffer.size() ) {
				value |= buffer[ cursor++ ] << (8*ii);
			} else {
				value |= 0x0 << (8*ii); // pad
			}
		}
		// std::cout << std::hex << value << std::dec << std::endl;
		track.push_back( value );
		count--;
	}
}
void parse_header( wave_format* data ) 
{
	size_t cursor = 0;
	data->header.magick = read_string( data->buffer, cursor, 4 );
	data->header.size = read32(data->buffer, cursor);
	data->header.type = read_string(data->buffer, cursor, 4 );
	data->header.format_chunk_marker = read_string(data->buffer, cursor, 4 );
	data->header.subchunk_size = read32( data->buffer, cursor );
	data->header.format_type = read16(data->buffer, cursor);
	data->header.num_channels = read16(data->buffer, cursor);
	data->header.sample_rate = read32(data->buffer, cursor);
	data->header.byte_rate = read32(data->buffer, cursor);
	data->header.block_align = read16(data->buffer, cursor);
	data->header.bits_per_sample = read16(data->buffer, cursor);
	data->header.data_chunk_header = read_string(data->buffer, cursor, 4);
	data->header.data_size = read32(data->buffer, cursor);
	// check we are a cannonical wave file
	assert( data->buffer.size() - 44 == data->header.data_size );
	data->data_begin_index = cursor;

	// calculate expected number of samples for check after we have read everything in
	data->num_samples = data->header.data_size / data->header.block_align / data->header.num_channels;

	// reserve space for N channels
	data->channels.resize(data->header.num_channels);

	size_t bytes_per_sample = data->header.block_align / data->header.num_channels;

	// read the rest of the audio in
	// channels are in order Chan1, Chan2, Chan1, Chan2
	while( cursor < data->buffer.size() ) {
		for( size_t track=0; track < data->header.num_channels; track++ ) {
			readN(data->buffer, cursor, bytes_per_sample, data->channels[track]);
		}
	}

	std::cout << data->channels[0].size() << std::endl;

	assert( data->channels[0].size() == data->num_samples );

	data->duration = (data->num_samples / data->header.sample_rate) * data->header.num_channels;
}

std::ostream& operator<< (std::ostream& out, wave_format const data ) 
{
	out << "Magick: " << data.header.magick << std::endl;
	out << "Size: " << data.header.size << std::endl;
	out << "type: " << data.header.type << std::endl;
	out << "format chunk marker: " << data.header.format_chunk_marker << std::endl;
	out << "subchunk size: " << data.header.subchunk_size << std::endl;
	out << "format type: " << data.header.format_type << std::endl;
	out << "num channels: " << data.header.num_channels << std::endl;
	out << "sample rate: " << data.header.sample_rate << std::endl;
	out << "byte rate: " << data.header.byte_rate << std::endl;
	out << "block align: " << data.header.block_align << std::endl;
	out << "bits per sample: " << data.header.bits_per_sample << std::endl;
	out << "data chunk header: " << data.header.data_chunk_header << std::endl;
	out << "data size: " << data.header.data_size << std::endl;
	return out;
}

std::ostream& operator<< (std::ostream& out, wave_format const * data ) 
{
	out << *data;
	return out;
}

wave_format* read_wave_file( const std::string filename ) {
	auto data = new wave_format();
	data->filename = filename;
	std::streampos size;
	std::ifstream wave_file(filename, std::ios::in | std::ios::binary | std::ios::ate );
	wave_file.unsetf(std::ios::skipws);
	if ( wave_file.is_open() ) {
		wave_file.seekg(0, std::ios::end);
		size = wave_file.tellg();
		data->buffer.reserve( size );
		wave_file.seekg( 0, std::ios::beg );
		data->buffer.insert( data->buffer.begin(), 
								std::istream_iterator<uint8_t>(wave_file),
								std::istream_iterator<uint8_t>());
	}
	wave_file.close();

	std::cout << "Read " << data->buffer.size() << " bytes" << std::endl;
	return data;	
}

void play_sound(wave_format* sound) {
	unsigned int pcm, tmp, dir;
	unsigned int rate, channels, seconds;
	snd_pcm_t *pcm_handle;
	snd_pcm_hw_params_t *params;
	snd_pcm_uframes_t frames;
	char *buff;
	int buff_size, loops;
	char const * pcm_device = "default";

	rate 	 = sound->header.sample_rate;
	channels = sound->header.num_channels;
	seconds = sound->duration;
	
	/* Open the PCM device in playback mode */
	if (pcm = snd_pcm_open(&pcm_handle, pcm_device,
					SND_PCM_STREAM_PLAYBACK, 0) < 0) 
		printf("ERROR: Can't open \"%s\" PCM device. %s\n",
					pcm_device, snd_strerror(pcm));

	/* Allocate parameters object and fill it with default values*/
	snd_pcm_hw_params_alloca(&params);

	snd_pcm_hw_params_any(pcm_handle, params);

	/* Set parameters */
	if (pcm = snd_pcm_hw_params_set_access(pcm_handle, params,
					SND_PCM_ACCESS_RW_INTERLEAVED) < 0) 
		printf("ERROR: Can't set interleaved mode. %s\n", snd_strerror(pcm));

	if (pcm = snd_pcm_hw_params_set_format(pcm_handle, params,
						SND_PCM_FORMAT_S16_LE) < 0) 
		printf("ERROR: Can't set format. %s\n", snd_strerror(pcm));

	if (pcm = snd_pcm_hw_params_set_channels(pcm_handle, params, channels) < 0) 
		printf("ERROR: Can't set channels number. %s\n", snd_strerror(pcm));

	if (pcm = snd_pcm_hw_params_set_rate_near(pcm_handle, params, &rate, 0) < 0) 
		printf("ERROR: Can't set rate. %s\n", snd_strerror(pcm));

	/* Write parameters */
	if (pcm = snd_pcm_hw_params(pcm_handle, params) < 0)
		printf("ERROR: Can't set harware parameters. %s\n", snd_strerror(pcm));

	/* Resume information */
	printf("PCM name: '%s'\n", snd_pcm_name(pcm_handle));

	printf("PCM state: %s\n", snd_pcm_state_name(snd_pcm_state(pcm_handle)));

	snd_pcm_hw_params_get_channels(params, &tmp);
	printf("channels: %i ", tmp);

	if (tmp == 1)
		printf("(mono)\n");
	else if (tmp == 2)
		printf("(stereo)\n");

	snd_pcm_hw_params_get_rate(params, &tmp, 0);
	printf("rate: %d bps\n", tmp);

	printf("seconds: %d\n", seconds);	

	/* Allocate buffer to hold single period */
	snd_pcm_hw_params_get_period_size(params, &frames, 0);

	// buff_size = frames * channels * 2 /* 2 -> sample size */;
	// buff = (char *) malloc(buff_size);

	snd_pcm_hw_params_get_period_time(params, &tmp, NULL);
	printf("period: %d\n", tmp);

	// load all sound frames at once
	if (pcm = snd_pcm_writei(pcm_handle, &sound->buffer.data()[sound->data_begin_index], sound->num_samples*sound->header.num_channels) == -EPIPE) {
		snd_pcm_prepare(pcm_handle);
	} else if (pcm < 0) {
		printf("ERROR. Can't write to PCM device. %s\n", snd_strerror(pcm));
	}
	
	// if (pcm = snd_pcm_writei(pcm_handle, &sound->channels[0], sound->num_samples) == -EPIPE) {
	// 	snd_pcm_prepare(pcm_handle);
	// } else if (pcm < 0) {
	// 	printf("ERROR. Can't write to PCM device. %s\n", snd_strerror(pcm));
	// }
	
	
	snd_pcm_drain(pcm_handle);
	snd_pcm_close(pcm_handle);

	return;
}

void save_as_png(wave_format* sound) 
{
	std::string filename = sound->filename + ".png";
	size_t ptr = sound->data_begin_index;
	// we create image as a square
	int width =	ceil(sqrt(sound->header.data_size) / 4);
	int height = ceil(sqrt(sound->header.data_size) / 4);
	float* buffer;
	int code = 0;
	FILE *fp = NULL;
	png_structp png_ptr = NULL;
	png_infop info_ptr = NULL;
	png_bytep row = NULL;

	fp = fopen(filename.c_str(), "wb");
   if (fp == NULL) {
      fprintf(stderr, "Could not open file %s for writing\n", filename);
      code = 1;
      goto finalise;
   }

   // Initialize write structure
   png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
   if (png_ptr == NULL) {
      fprintf(stderr, "Could not allocate write struct\n");
      code = 1;
      goto finalise;
   }

   // Initialize info structure
   info_ptr = png_create_info_struct(png_ptr);
   if (info_ptr == NULL) {
      fprintf(stderr, "Could not allocate info struct\n");
      code = 1;
      goto finalise;
   }

   // Setup Exception handling
   if (setjmp(png_jmpbuf(png_ptr))) {
      fprintf(stderr, "Error during png creation\n");
      code = 1;
      goto finalise;
   }
	
	png_init_io(png_ptr, fp);

   // Write header (8 bit colour depth)
   png_set_IHDR(png_ptr, info_ptr, width, height,
         8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
         PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

   png_write_info(png_ptr, info_ptr);

    // Allocate memory for one row (3 bytes per pixel - RGB)
   row = (png_bytep) malloc(4 * width * sizeof(png_byte));

   // Write image data
   int x, y;
   
   for (y=0 ; y<height*4 ; y++) {
      for (x=0 ; x<width*4 ; x++) {
		  if ( ptr < sound->buffer.size() ) {
         	row[x] = sound->buffer[ptr];
		 	ptr++;
		  } else {
			row[x] = 0;
		  }
      }
      png_write_row(png_ptr, row);
   }

   // End write
   png_write_end(png_ptr, NULL);

finalise:
   if (fp != NULL) fclose(fp);
   if (info_ptr != NULL) png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
   if (png_ptr != NULL) png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
   if (row != NULL) free(row);

}

int main(int argc, char ** argv ) 
{
	std::string filename = argv[1];
	std::cout << "Reading " << filename << std::endl;
	auto wave = read_wave_file( filename );
	parse_header(wave);
	std::cout << wave << std::endl;
	//play_sound(wave);
	save_as_png(wave);
	delete wave;
	return 0;
}


