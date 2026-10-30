# 第三方元件聲明（Third-Party Notices）

PDF to JPG_QT 本身以 MIT 授權釋出（見 `LICENSE`），著作權 © 2026 Kaido-MSVC。
本程式散布時附帶下列第三方元件，各元件依其原授權使用，不受本專案的 MIT 授權涵蓋。
授權全文位於 `licenses/` 資料夾，**每個元件一份檔案**，內含該元件原本的著作權聲明與授權條文。

資料來源：
- Qt Core／GUI／Network／Image Formats 的第三方元件：Qt 6.12.0 官方文件的各元件 attribution 頁
  （隨 Qt 安裝於 `Docs/Qt-6.12.0/<模組>/<模組>-attribution-*.html`，線上版見
  https://doc.qt.io/qt-6/licenses-used-in-qt.html ），著作權聲明與條文逐字取自該頁。
- XSVG：本機文件沒有它的 attribution 頁，條文取自 qtbase v6.12.0 原始碼的 `src/gui/util/LICENSE.XSVG.txt`
  （與同目錄 `qguisvg.cpp` 內的聲明逐字相同）。
- Qt PDF 內含的元件：Qt 6.12.0 隨附 SBOM `qtwebengine-chromium-pdf-6.140.0.spdx.json` 的
  `LicenseRef-QtWebEngine-Chromium-Pdf-*` 條文，與官方頁 https://doc-snapshots.qt.io/qtpdf/qtpdf-licensing.html 一致。
- 只列出隨本程式散布之 DLL 與外掛實際內含的元件。

## 1. Qt 6（LGPLv3）

| 項目 | 內容 |
|---|---|
| 元件 | Qt Core、Qt GUI、Qt Widgets、Qt Network、Qt SVG（6.12.0）；Qt PDF 6.140.0（隨 Qt 6.12.0 發佈）；Qt Image Formats 外掛（6.12.0）；Windows 平台、樣式、TLS、網路資訊、圖示引擎、TUIO 外掛（6.12.0） |
| 著作權 | Copyright (C) The Qt Company Ltd. and other contributors |
| 授權 | GNU Lesser General Public License v3（`licenses/LGPL-3.0.txt`，其引用之 GPLv3 見 `licenses/GPL-3.0.txt`；Qt 原始授權檔見 `licenses/Qt-LICENSE.txt`） |
| 官方網址 | https://www.qt.io/ |
| 原始碼 | 本程式使用未修改的官方 Qt 二進位版本。原始碼可由 https://download.qt.io/official_releases/qt/6.12/6.12.0/ 或 https://code.qt.io/ 取得；qtbase 對應 commit `025bdad181de241e81bf853c8a2d7bf3d19261a9`，qtpdf 對應 commit `d505fc23640d2bd6345e6483eb73daf4e19f5829`（git://code.qt.io/qt/qtpdf.git） |

### 連結方式與替換 Qt

- 本程式以**動態連結**使用 Qt，Qt DLL（`Qt6*.dll`）與外掛資料夾（`platforms/`、`imageformats/` 等）以獨立檔案放在執行檔旁。
- 使用者可以自行以相容、ABI 一致的 Qt 6.12.x（MSVC 2022 x64）DLL 替換這些檔案；程式不做簽章或雜湊驗證以阻擋替換。
- 本專案的 MIT 授權允許修改與為除錯目的進行反向工程。
- 如需索取本程式所用 Qt 版本的原始碼，可依上方網址取得，或聯絡作者 Kaido-MSVC。

## 2. Qt 內含的第三方元件

### 2.1 Qt PDF 內含（PDFium 等）

隨 Qt PDF 6.140.0（隨 Qt 6.12.0 發佈）散布。版本取自 SBOM，SBOM 未標示者留空。

| 元件 | 版本 | 授權 | 官方網址 | 授權檔 |
|---|---|---|---|---|
| PDFium（Copyright 2014 The PDFium Authors） | — | BSD-3-Clause；部分檔案 Apache-2.0 | https://pdfium.googlesource.com/pdfium/ | `QtPdf-pdfium.txt` |
| The Chromium Project（Copyright 2015 The Chromium Authors） | — | BSD-3-Clause | https://www.chromium.org/ | `QtPdf-chromium.txt` |
| Abseil | — | Apache-2.0 | https://abseil.io/ | `QtPdf-abseil.txt` |
| FreeType | VER-2-13-3-307-g27c1cb10a | FreeType License (FTL) | https://freetype.org/ | `QtPdf-freetype.txt` |
| ICU | 74-2 | Unicode-3.0 / ICU | https://icu.unicode.org/ | `QtPdf-icu.txt` |
| fast_float（Copyright (c) 2021 The fast_float authors） | 7.0.0 | MIT | https://github.com/fastfloat/fast_float | `QtPdf-fast_float.txt` |
| libjpeg-turbo | 3.1.0 | IJG、BSD-3-Clause、Zlib | https://libjpeg-turbo.org/ | `QtPdf-libjpeg-turbo.txt` |
| libpng | 1.6.58 | libpng-2.0、Libpng | http://www.libpng.org/pub/png/libpng.html | `QtPdf-libpng.txt` |
| zlib | 1.3.1 | Zlib | https://zlib.net/ | `QtPdf-zlib.txt` |

### 2.2 Qt Core（Qt6Core.dll）

| 元件 | 版本 | 授權 | 著作權 | 網址 | 授權檔 |
|---|---|---|---|---|---|
| PCRE2 | 10.48 | BSD-3-Clause（含 PCRE2 二進位套件例外） | University of Cambridge; Philip Hazel; Zoltan Herczeg | https://github.com/PCRE2Project/pcre2 | `QtCore-pcre2.txt` |
| PCRE2 sljit | 10.48 | BSD-2-Clause | Zoltan Herczeg | https://github.com/PCRE2Project/pcre2 | `QtCore-pcre2-sljit.txt` |
| zlib（Qt Core、Qt GUI、Qt Network 共用） | 1.3.2 | Zlib | Jean-loup Gailly and Mark Adler | https://zlib.net/ | `QtBase-zlib.txt` |
| Unicode Character Database | 36 | Unicode-3.0 | Unicode, Inc. | https://www.unicode.org/ucd/ | `QtCore-unicode-character-database.txt` |
| Unicode CLDR | v48.2 | Unicode-3.0 | Unicode, Inc. | https://cldr.unicode.org/ | `QtCore-unicode-cldr.txt` |
| double-conversion | 3.4.0 | BSD-3-Clause | the V8 project authors | https://github.com/google/double-conversion | `QtCore-double-conversion.txt` |
| TinyCBOR | 7.0 | MIT | Intel Corporation | https://github.com/intel/tinycbor | `QtCore-tinycbor.txt` |
| Easing equations | — | BSD-3-Clause | Robert Penner | http://robertpenner.com/easing/ | `QtCore-easing.txt` |
| RFC 6234（SHA 參考實作） | — | BSD-3-Clause | IETF Trust and the persons identified as authors of the code | https://www.rfc-editor.org/rfc/rfc6234 | `QtCore-rfc6234.txt` |
| SHA-3 endian helpers | 1.0.0 | BSD-2-Clause | Brian Gladman | — | `QtCore-sha3-endian.txt` |
| Keccak (SHA-3) | 3.2 | CC0-1.0 | — | — | `QtCore-sha3-keccak.txt` |
| Apache Tika MIME types | c2972478 | Apache-2.0 | The Apache Software Foundation | https://tika.apache.org/ | `QtCore-tika-mimetypes.txt` |
| BLAKE2 | ed1974ea | CC0-1.0 或 Apache-2.0 | Samuel Neves | https://github.com/BLAKE2/BLAKE2 | `QtCore-blake2.txt` |
| tl::expected | 41d3e1f4 | CC0-1.0 | Sy Brand | https://github.com/TartanLlama/expected | `QtCore-tlexpected.txt` |
| SipHash | — | CC0-1.0 | Jean-Philippe Aumasson; Daniel J. Bernstein; Intel Corporation | — | `QtCore-siphash.txt` |
| MD4 | — | CC0-1.0（公有領域） | Alexander Peslyak | — | `QtCore-md4.txt` |
| MD5 | — | CC0-1.0（公有領域） | Colin Plumb | — | `QtCore-md5.txt` |
| SHA-1（CSHA1） | — | 公有領域 | Dominik Reichl | http://www.dominik-reichl.de/ | `QtCore-sha1.txt` |

### 2.3 Qt GUI（Qt6Gui.dll）

| 元件 | 版本 | 授權 | 著作權 | 網址 | 授權檔 |
|---|---|---|---|---|---|
| FreeType 2 | 2.14.3 | FTL（另可選 GPLv2，本程式選用 FTL） | David Turner, Robert Wilhelm, Werner Lemberg 等 | https://freetype.org/ | `QtGui-freetype.txt` |
| FreeType BDF 驅動 | — | MIT | 見授權檔 | https://freetype.org/ | `QtGui-freetype-bdf.txt` |
| FreeType PCF 驅動 | — | MIT 及 MIT-open-group | 見授權檔 | https://freetype.org/ | `QtGui-freetype-pcf.txt` |
| FreeType zlib 子元件 | — | Zlib | 見授權檔 | https://freetype.org/ | `QtGui-freetype-zlib.txt` |
| FreeType 灰階光柵器 | — | FTL（另可選 GPLv2） | David Turner, Robert Wilhelm, Werner Lemberg | https://freetype.org/ | `QtGui-grayraster.txt` |
| HarfBuzz | 14.5.0 | MIT | Google, Inc. 等 | https://harfbuzz.github.io/ | `QtGui-harfbuzz-ng.txt` |
| libjpeg-turbo | 3.2.0 | IJG、BSD-3-Clause | D. R. Commander; Thomas G. Lane 等 | https://libjpeg-turbo.org/ | `QtGui-libjpeg-turbo.txt` |
| libpng | 1.6.58 | Libpng、libpng-2.0 | The PNG Reference Library Authors 等 | http://www.libpng.org/ | `QtGui-libpng.txt` |
| zlib | 1.3.2 | Zlib | Jean-loup Gailly and Mark Adler | https://zlib.net/ | `QtBase-zlib.txt` |
| MD4C | 0.5.3 | MIT | Martin Mitáš | https://github.com/mity/md4c | `QtGui-md4c.txt` |
| Emoji Segmenter | 0.4.0 | Apache-2.0 | Google LLC | https://github.com/google/emoji-segmenter | `QtGui-emoji-segmenter.txt` |
| Adobe Glyph List For New Fonts | 1.7 | BSD-3-Clause | Adobe Systems | https://github.com/adobe-type-tools/agl-aglfn | `QtGui-aglfn.txt` |
| D3D12 Memory Allocator | v3.1.0 | MIT | Advanced Micro Devices, Inc. | https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator | `QtGui-d3d12memoryallocator.txt` |
| Vulkan Memory Allocator | 3.3.0 | MIT | Advanced Micro Devices, Inc. | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator | `QtGui-vulkanmemoryallocator.txt` |
| D3D12 Mipmap generator（MiniEngine） | 0aa79bad | MIT | Microsoft | https://github.com/microsoft/DirectX-Graphics-Samples | `QtGui-miniengine-d3d12-mipmap.txt` |
| Vulkan API Registry | 1.4.308 | Apache-2.0 或 MIT | The Khronos Group Inc. | https://www.khronos.org/ | `QtGui-vulkan-xml-spec.txt` |
| OpenGL Headers | Rev. 27684 | MIT | The Khronos Group Inc. | https://www.khronos.org/ | `QtGui-opengl-headers.txt` |
| OpenGL ES 2 Headers | Rev. 27673 | MIT | The Khronos Group Inc. | https://www.khronos.org/ | `QtGui-opengl-es2-headers.txt` |
| WebGradients | — | MIT | itmeo | https://webgradients.com/ | `QtGui-webgradients.txt` |
| Smooth Scaling Algorithm | — | BSD-2-Clause 及 Imlib2 | Daniel M. Duley; Carsten Haitzler 等 | — | `QtGui-smooth-scaling-algorithm.txt` |
| X Server helper | — | X11 及 HPND | X Consortium; Digital Equipment Corporation | https://www.x.org/ | `QtGui-xserverhelper.txt` |
| sRGB color profile | — | ICC License | International Color Consortium | http://www.color.org/ | `QtGui-icc-srgb-color-profile.txt` |
| XSVG（弧線處理程式碼，位於 qtbase `src/gui/util/qguisvg.cpp`） | — | HPND-sell-variant | USC/Information Sciences Institute | — | `QtGui-xsvg.txt` |

### 2.4 Qt Network（Qt6Network.dll）

| 元件 | 版本 | 授權 | 著作權 | 網址 | 授權檔 |
|---|---|---|---|---|---|
| Public Suffix List 資料 | 2026-05-14 | MPL-2.0（原始資料：https://publicsuffix.org/list/public_suffix_list.dat） | Public Suffix List contributors | https://publicsuffix.org/ | `QtNetwork-psl-data.txt` |
| libpsl | 664f3dc8 | BSD-3-Clause | The Chromium Authors | https://github.com/rockdaboot/libpsl | `QtNetwork-libpsl.txt` |
| zlib | 1.3.2 | Zlib | Jean-loup Gailly and Mark Adler | https://zlib.net/ | `QtBase-zlib.txt` |

### 2.5 Qt 外掛

| 外掛 | 元件 | 版本 | 授權 | 網址 | 授權檔 |
|---|---|---|---|---|---|
| platforms/qwindows.dll | Wintab API | — | LCS/Telegraphics | — | `QtGui-wintab.txt` |
| imageformats/qtiff.dll | libtiff | 4.7.2 | libtiff | https://libtiff.gitlab.io/libtiff/ | `QtImageFormats-libtiff.txt` |
| imageformats/qwebp.dll | libwebp | 1.6.0 | BSD-3-Clause | https://chromium.googlesource.com/webm/libwebp | `QtImageFormats-libwebp.txt` |

### 2.6 必要致謝聲明

- Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved.
- This software is based in part on the work of the Independent JPEG Group.
- PDF 為 Adobe 的商標；本程式與 Adobe 無關。

## 3. Microsoft 可再散布元件

下列檔案由 Microsoft 依其可再散布條款提供，不屬於開源授權，亦不受本專案 MIT 授權涵蓋：

| 檔案 | 來源 | 版本 | 授權 |
|---|---|---|---|
| msvcp140*.dll、vcruntime140*.dll、concrt140.dll、vccorlib140.dll | Microsoft Visual C++ Redistributable | 14.52.36815 | Microsoft Visual Studio 授權條款之「可散發程式碼」 |
| d3dcompiler_47.dll、dxcompiler.dll、dxil.dll | Windows SDK Redist（由 windeployqt 部署，供 Qt RHI 使用） | 10.0.28000 / 1.9.2602 / 101.6.2112 | Microsoft Windows SDK 授權條款 |

## 圖示與素材

本程式圖示（assets/app.ico、assets/app.png）由作者 Kaido-MSVC 使用 OpenAI ChatGPT 圖像生成功能製作。
依 OpenAI 使用條款，生成內容的權利歸使用者所有，隨本專案以 MIT 授權散布。
