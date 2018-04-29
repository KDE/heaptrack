/*
 * Copyright 2018 Milian Wolff <mail@milianw.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef TEMPFILE_H
#define TEMPFILE_H

#include <boost/filesystem.hpp>
#include <fcntl.h>
#include <unistd.h>

#include <fstream>
#include <sstream>

struct TempFile
{
    TempFile()
        : path(boost::filesystem::unique_path())
        , fileName(path.native())
    {
    }

    ~TempFile()
    {
        boost::filesystem::remove(path);
        close();
    }

    bool open()
    {
        fd = ::open(fileName.c_str(), O_CREAT | O_CLOEXEC | O_RDWR, 0644);
        return fd != -1;
    }

    void close()
    {
        if (fd != -1) {
            ::close(fd);
        }
    }

    std::string readContents() const
    {
        // open in binary mode to really read everything
        // we want to ensure that the contents are really clean
        std::ifstream ifs(fileName, std::ios::binary);
        return {std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
    }

    const boost::filesystem::path path;
    const std::string fileName;
    int fd = -1;
};

#endif
