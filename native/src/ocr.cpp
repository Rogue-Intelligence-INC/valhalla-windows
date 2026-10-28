#include "ocr.h"
#include "utf.h"

#include <cstring>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

#include <windows.h>

#include <algorithm>
#include <vector>

namespace {

bool apartment_ready = false;

void ensure_apartment() {
    if (apartment_ready) return;
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    } catch (const winrt::hresult_error&) {
    }
    apartment_ready = true;
}

winrt::Windows::Media::Ocr::OcrEngine make_engine() {
    using namespace winrt::Windows::Media::Ocr;
    using namespace winrt::Windows::Globalization;
    OcrEngine engine = OcrEngine::TryCreateFromUserProfileLanguages();
    if (!engine) engine = OcrEngine::TryCreateFromLanguage(Language(L"zh-Hans"));
    if (!engine) engine = OcrEngine::TryCreateFromLanguage(Language(L"en-US"));
    return engine;
}

template <typename T>
T wait_operation(winrt::Windows::Foundation::IAsyncOperation<T> operation, std::wstring& error) {
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    T result{nullptr};
    operation.Completed([done, &result](auto const& sender, winrt::Windows::Foundation::AsyncStatus status) {
        if (status == winrt::Windows::Foundation::AsyncStatus::Completed) {
            try {
                result = sender.GetResults();
            } catch (const winrt::hresult_error&) {
            }
        }
        SetEvent(done);
    });
    DWORD wait = WAIT_TIMEOUT;
    while ((wait = MsgWaitForMultipleObjects(1, &done, FALSE, 20000, QS_ALLINPUT)) == WAIT_OBJECT_0 + 1) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(msg.wParam));
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CloseHandle(done);
    if (wait == WAIT_TIMEOUT) error = L"OCR timed out";
    return result;
}

}  // namespace

OcrPass recognize_bgra(const std::uint8_t* pixels, int width, int height, int stride) {
    OcrPass pass;
    if (!pixels || width < 8 || height < 8 || stride < width * 4) {
        pass.error = L"image too small";
        return pass;
    }
    try {
        ensure_apartment();
        std::vector<std::uint8_t> packed(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        for (int y = 0; y < height; ++y) {
            const std::uint8_t* row = pixels + static_cast<size_t>(y) * static_cast<size_t>(stride);
            std::uint8_t* dest = packed.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
            memcpy(dest, row, static_cast<size_t>(width) * 4);
            for (int x = 0; x < width; ++x) dest[x * 4 + 3] = 255;
        }
        winrt::Windows::Storage::Streams::DataWriter writer;
        writer.WriteBytes(winrt::array_view<const std::uint8_t>(packed.data(), packed.data() + packed.size()));
        auto buffer = writer.DetachBuffer();
        auto bitmap = winrt::Windows::Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(
            buffer, winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8, width, height,
            winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Ignore);
        auto engine = make_engine();
        if (!engine) {
            pass.error = L"Windows OCR is not installed";
            return pass;
        }
        std::wstring wait_error;
        auto result = wait_operation(engine.RecognizeAsync(bitmap), wait_error);
        if (!wait_error.empty()) {
            pass.error = wait_error;
            return pass;
        }
        if (!result) {
            pass.error = L"OCR returned nothing";
            return pass;
        }
        for (auto const& line : result.Lines()) {
            OcrBox box;
            box.text = line.Text().c_str();
            bool any = false;
            float left = 0;
            float top = 0;
            float right = 0;
            float bottom = 0;
            for (auto const& word : line.Words()) {
                auto rect = word.BoundingRect();
                OcrBox piece;
                piece.text = word.Text().c_str();
                piece.x = static_cast<int>(rect.X);
                piece.y = static_cast<int>(rect.Y);
                piece.w = std::max(1, static_cast<int>(rect.Width));
                piece.h = std::max(1, static_cast<int>(rect.Height));
                if (!piece.text.empty()) pass.words.push_back(piece);
                float word_right = rect.X + rect.Width;
                float word_bottom = rect.Y + rect.Height;
                if (!any) {
                    left = rect.X;
                    top = rect.Y;
                    right = word_right;
                    bottom = word_bottom;
                    any = true;
                } else {
                    left = std::min(left, rect.X);
                    top = std::min(top, rect.Y);
                    right = std::max(right, word_right);
                    bottom = std::max(bottom, word_bottom);
                }
            }
            if (any) {
                box.x = static_cast<int>(left);
                box.y = static_cast<int>(top);
                box.w = std::max(1, static_cast<int>(right - left));
                box.h = std::max(1, static_cast<int>(bottom - top));
            }
            if (!box.text.empty()) pass.lines.push_back(std::move(box));
        }
        pass.ok = true;
    } catch (const winrt::hresult_error& error) {
        pass.error = error.message().c_str();
    } catch (const std::exception& error) {
        pass.error = utf8_to_wide(error.what());
    }
    return pass;
}
