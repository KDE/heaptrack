/*
    SPDX-FileCopyrightText: 2018 Milian Wolff <mail@milianw.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
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
