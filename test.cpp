#include <cstdio>	//stdandard output
#include <cstdlib>
#include <ctime>

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Types.hpp>
#include <SoapySDR/Formats.hpp>

#include <string>	// std::string
#include <vector>	// std::vector<...>
#include <map>		// std::map< ... , ... >

#include <iostream>

#include <unistd.h>

#include <complex.h>
#include <fftw3.h>
#include <omp.h>

int main()
{

	// 0. enumerate devices (list all devices' information)
	SoapySDR::KwargsList results = SoapySDR::Device::enumerate();
	SoapySDR::Kwargs::iterator it;

	for( int i = 0; i < results.size(); ++i)
	{
		printf("Found device #%d: ", i);
		for( it = results[i].begin(); it != results[i].end(); ++it)
		{
			printf("%s = %s\n", it->first.c_str(), it->second.c_str());
		}
		printf("\n");
	}

	// 1. create device instance
	
	//	1.1 set arguments
	//		args can be user defined or from the enumeration result
	//		We use first results as args here:
	SoapySDR::Kwargs args = results[0];

	//	1.2 make device
	SoapySDR::Device *sdr = SoapySDR::Device::make(args);

	if( sdr == NULL )
	{
		fprintf(stderr, "SoapySDR::Device::make failed\n");
		return EXIT_FAILURE;
	}

	// 2. query device info
	std::vector< std::string > str_list;	//string list

	//	2.1 antennas
	str_list = sdr->listAntennas( SOAPY_SDR_RX, 0);
	printf("Rx antennas: ");
	for(int i = 0; i < str_list.size(); ++i)
		printf("%s,", str_list[i].c_str());
	printf("\n");

	//	2.2 gains
	str_list = sdr->listGains( SOAPY_SDR_RX, 0);
	printf("Rx Gains: ");
	for(int i = 0; i < str_list.size(); ++i)
		printf("%s, ", str_list[i].c_str());
	printf("\n");

	//	2.3. ranges(frequency ranges)
	SoapySDR::RangeList ranges = sdr->getFrequencyRange( SOAPY_SDR_RX, 0);
	printf("Rx freq ranges: ");
	for(int i = 0; i < ranges.size(); ++i)
		printf("[%g Hz -> %g Hz], ", ranges[i].minimum(), ranges[i].maximum());
	printf("\n");

	// 3. apply settings
	long sample_rate = 16e6;
	sdr->setSampleRate( SOAPY_SDR_RX, 0, sample_rate);

	sdr->setFrequency( SOAPY_SDR_RX, 0, 433e6);

	// 4. setup a stream (complex floats)
	SoapySDR::Stream *rx_stream = sdr->setupStream( SOAPY_SDR_RX, SOAPY_SDR_CF32);
	if( rx_stream == NULL)
	{
		fprintf( stderr, "Failed\n");
		SoapySDR::Device::unmake( sdr );
		return EXIT_FAILURE;
	}
	sdr->activateStream( rx_stream, 0, 0, 0);

	bool do_fft = false;
	long Ns = 8*128*1024;
	int mult = 1;
	long N = mult*Ns;
	int wait_time_us = N/16e3;

	fftw_complex *in, *out;
	fftw_plan p;
	
	if (fftw_init_threads() == 0)
	{
		printf("multithread error\n");	
	}
	int nthreads = 6;
	printf("using %d threads\n",nthreads);

	in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
	out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
	
	fftw_plan_with_nthreads(nthreads);
	p = fftw_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_MEASURE);

	// 5. create a re-usable buffer for rx samples
	//fftw_complex buff[Ns];

	int flags, delay;
	clock_t start, inter, end;
	long long time_ns=0, timeprev_ns=0;

	//sleep(1);

	// 6. receive some samples
	printf("sample duration %.0f us\n",((float)Ns)*1e6/sample_rate);
	for( int j = 0; j < 30; ++j)
	{
		fftw_complex *cur = in;
		void *buffs[] = {cur};
		start = clock();
		for( int i = 0; i < mult; ++i)
		{
			//timeprev_ns = time_ns;
			int ret = sdr->readStream(rx_stream, buffs, Ns, flags, time_ns, 2e6);
			if (ret < Ns)
			{
				printf("Samples dropped\n");
			}
			//printf("ret = %d, flags = %d, time_ns = %lld\n", ret, flags, time_ns-timeprev_ns);
			cur += Ns;							
		}
		inter = clock();
		if (do_fft) 
		{
			fftw_execute(p);
		}
		end = clock();
		printf("read time %.0f us, fft time %.0f us, total %.0f us\n",((float)(inter-start)*1e6)/CLOCKS_PER_SEC,((float)(end-inter)*1e6)/CLOCKS_PER_SEC,((float)(end-start)*1e6)/CLOCKS_PER_SEC);
	}

	// 7. shutdown the stream
	sdr->deactivateStream( rx_stream, 0, 0);	//stop streaming
	sdr->closeStream( rx_stream );

	// 8. cleanup device handle
	fftw_destroy_plan(p);
	fftw_free(in); fftw_free(out);

	SoapySDR::Device::unmake( sdr );
	printf("Done\n");

	return EXIT_SUCCESS;
}
