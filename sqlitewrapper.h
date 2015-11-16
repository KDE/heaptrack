/*
 * Copyright 2015 Milian Wolff <mail@milianw.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <sqlite3.h>

#include <string>
#include <exception>
#include <stdexcept>
#include <memory>
#include <cstring>

#ifndef SQLITE_CPP_WRAPPER
#define SQLITE_CPP_WRAPPER

namespace sqlite {

using Database = std::shared_ptr<sqlite3>;

Database open(const std::string& filename)
{
    sqlite3* connection;
    int rc = sqlite3_open(filename.c_str(), &connection);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Could not open '" + filename + "'");
    }
    return std::shared_ptr<sqlite3>(connection, [] (sqlite3* connection) {
        sqlite3_close(connection);
    });
}

class Query
{
public:
    Query(const Database& db, const std::string& query = {})
        : m_db(db)
    {
        if (!query.empty()) {
            prepare(query);
        }
    }

    Query(Query&& rhs)
        : m_db(rhs.m_db)
        , m_statement(rhs.m_statement)
    {
        rhs.m_statement = nullptr;
    }

    ~Query()
    {
        clear();
    }

    void clear()
    {
        if (m_statement) {
            sqlite3_finalize(m_statement);
        }
    }

    void prepare(const std::string& query)
    {
        clear();

        int rc = sqlite3_prepare_v2(m_db.get(), query.c_str(), query.length(),
                                    &m_statement, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("Failed to prepare query: \"" + query + "\": " + sqlite3_errmsg(m_db.get()));
        }
    }

    void reset()
    {
        int rc = sqlite3_reset(m_statement);
        if (rc != SQLITE_OK) {
            throw std::runtime_error(std::string("Failed to reset query: ") + sqlite3_errmsg(m_db.get()));
        }
    }

    bool execute()
    {
        return sqlite3_step(m_statement) != SQLITE_DONE;
    }

    void bind(int where, const char* text, int length = -1, bool staticData = true)
    {
        if (length == -1) {
            length = strlen(text);
        }
        int rc = sqlite3_bind_text(m_statement, where, text, length, staticData ? SQLITE_STATIC : SQLITE_TRANSIENT);
        if (rc != SQLITE_OK) {
            throw std::runtime_error(std::string("Failed to bind: ") + sqlite3_errmsg(m_db.get()));
        }
    }

    void bind(int where, std::string&& text)
    {
        bind(where, text.c_str(), text.size(), false);
    }

    void bind(int where, const std::string& text)
    {
        bind(where, text.c_str(), text.size());
    }

    void bind(int where, uint64_t number)
    {
        int rc = sqlite3_bind_int64(m_statement, where, static_cast<sqlite3_int64>(number));
        if (rc != SQLITE_OK) {
            throw std::runtime_error(std::string("Failed to bind: ") + sqlite3_errmsg(m_db.get()));
        }
    }

    template<typename T>
    void bindAll(int i, T v)
    {
        bind(i, v);
    }

    template<typename T, typename... Args>
    void bindAll(int i, T first, Args... args) {
        bindAll(i, first);
        bindAll(i + 1, args...);
    }

private:
    Database m_db;
    sqlite3_stmt* m_statement = nullptr;
};

class InsertQuery : public Query
{
public:
    InsertQuery(const Database& db, const std::string& query)
        : Query(db, query)
    {}

    template<typename... Args>
    void insert(Args... args)
    {
        bind(1, m_id++);
        bindAll(2, args...);
        execute();
        reset();
    }

    uint64_t rowsInserted() const
    {
        return m_id;
    }

private:
    uint64_t m_id = 0;
};

void execute(const Database& db, const std::string& queryString)
{
    Query q(db, queryString);
    q.execute();
}

}

#endif