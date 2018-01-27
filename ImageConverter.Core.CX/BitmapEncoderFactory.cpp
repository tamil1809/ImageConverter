#include "pch.h"
#include "pplawait.h"
#include "BitmapEncoderFactory.h"
#include "BitmapConversionSettings.h"
#include <concurrent_vector.h>

using namespace ImageConverter::Core::CX;
using namespace Platform;
using namespace Platform::Collections;
using namespace concurrency;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Storage;

BitmapEncoderFactory::BitmapEncoderFactory()
{

}

task<BitmapConversionResult^> BitmapEncoderFactory::ConvertAsync(
	StorageFile^ file,
	IStorageFolder^ targetFolder,
	BitmapConversionSettings^ settings)
{
	BitmapConversionResult^ result = ref new BitmapConversionResult();

	// 1. Open input
	auto inputStream = co_await file->OpenAsync(FileAccessMode::Read);

	// 2. Create decoder
	BitmapDecoder^ decoder;
	try
	{
		decoder = co_await BitmapDecoder::CreateAsync(inputStream);
	}
	catch (Exception^ ex)
	{
		result->Status = "Could not decode file";
		return result;
	}
	
	// 3. Create output file
	StorageFile^ outputFile; 
	IRandomAccessStream^ outputStream;
	try
	{
		outputFile = co_await targetFolder->CreateFileAsync(file->DisplayName + settings->FileExtension, settings->CollisionOption);
		outputStream = co_await outputFile->OpenAsync(FileAccessMode::ReadWrite);
	}
	catch (Exception^ ex)
	{
		result->Status = "Could not create output file";
		return result;
	}
	
	// 4. Encode to output file
	co_await EncodeInternalAsync(decoder, settings->EncoderId, outputStream, settings->Options);
	delete decoder;
	delete outputStream;

	// 5. Get file size
	auto properties = co_await outputFile->GetBasicPropertiesAsync();
	result->ResultFileSize = properties->Size;
	result->Success = true;

	return result;
}

IAsyncOperation<BitmapConversionResult^>^  BitmapEncoderFactory::EncodeAsync(
	StorageFile^ file,
	IStorageFolder^ targetFolder,
	BitmapConversionSettings^ settings)
{
	return create_async([file, targetFolder, settings]
		{
			return ConvertAsync(file, targetFolder, settings);
		}); 
}

IAsyncOperation<BitmapEncoder^>^ BitmapEncoderFactory::CreateEncoderAsync(
	Platform::Guid id, 
	IRandomAccessStream^ stream,
	IVectorView<BitmapOption^>^ options)
{
	auto map = ref new Map<Platform::String^, BitmapTypedValue^>();
	for each (BitmapOption^ var in options)
	{
		map->Insert(var->Name, var->Value);
	}

	return BitmapEncoder::CreateAsync(id, stream, map);
}

IAsyncAction^ BitmapEncoderFactory::EncodeInternalAsync(
	BitmapDecoder^ decoder,
	Platform::Guid encoderId,
	IRandomAccessStream^ outputStream,
	IVectorView<BitmapOption^>^ options)
{
	outputStream->Size = 0;
	return create_async([decoder, encoderId, outputStream, options]
	{
		return create_task(CreateEncoderAsync(encoderId, outputStream, options)).then([decoder](BitmapEncoder^ encoder)
		{
			return create_task(decoder->GetPixelDataAsync()).then([encoder, decoder](PixelDataProvider^ pixeldata)
			{
				encoder->SetPixelData(
					decoder->BitmapPixelFormat,
					decoder->BitmapAlphaMode,
					decoder->OrientedPixelWidth,
					decoder->OrientedPixelHeight,
					decoder->DpiX,
					decoder->DpiY,
					pixeldata->DetachPixelData());

				return create_task(encoder->FlushAsync()).then([encoder]
				{
					delete encoder;
				}, task_continuation_context::use_arbitrary());
			}, task_continuation_context::use_arbitrary());
		}, task_continuation_context::use_arbitrary());
	});
}