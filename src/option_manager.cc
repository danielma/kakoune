#include "option_manager.hh"

#include "assert.hh"

namespace Kakoune
{

OptionDesc::OptionDesc(String name, String docstring, OptionFlags flags)
    : m_name(std::move(name)), m_docstring(std::move(docstring)),
    m_flags(flags) {}

Option::Option(const OptionDesc& desc, OptionManager& manager)
    : m_manager(manager), m_desc(desc) {}

OptionManager::OptionManager(OptionManager& parent)
    : m_parent(&parent)
{
    parent.register_watcher(*this);
}

OptionManager::~OptionManager()
{
    if (m_parent)
        m_parent->unregister_watcher(*this);

    kak_assert(m_watchers.empty());
}

void OptionManager::register_watcher(OptionManagerWatcher& watcher)
{
    kak_assert(not contains(m_watchers, &watcher));
    m_watchers.push_back(&watcher);
}

void OptionManager::unregister_watcher(OptionManagerWatcher& watcher)
{
    auto it = find(m_watchers.begin(), m_watchers.end(), &watcher);
    kak_assert(it != m_watchers.end());
    m_watchers.erase(it);
}

struct option_not_found : public runtime_error
{
    option_not_found(StringView name)
        : runtime_error("option not found: " + name) {}
};

Option& OptionManager::get_local_option(StringView name)
{
    auto it = find_option(m_options, name);
    if (it != m_options.end())
        return **it;
    else if (m_parent)
    {
        m_options.emplace_back((*m_parent)[name].clone(*this));
        return *m_options.back();
    }
    else
        throw option_not_found(name);

}

Option& OptionManager::operator[](StringView name)
{
    auto it = find_option(m_options, name);
    if (it != m_options.end())
        return **it;
    else if (m_parent)
        return (*m_parent)[name];
    else
        throw option_not_found(name);
}

const Option& OptionManager::operator[](StringView name) const
{
    return const_cast<OptionManager&>(*this)[name];
}

template<typename MatchingFunc>
CandidateList OptionManager::get_matching_names(MatchingFunc func)
{
    CandidateList result;
    if (m_parent)
        result = m_parent->get_matching_names(func);
    for (auto& option : m_options)
    {
        if (option->flags() & OptionFlags::Hidden)
            continue;

        const auto& name = option->name();
        if (func(name) and not contains(result, name))
            result.push_back(name);
    }
    return result;
}

CandidateList OptionManager::complete_option_name(StringView prefix,
                                                  ByteCount cursor_pos)
{
    using namespace std::placeholders;
    auto real_prefix = prefix.substr(0, cursor_pos);
    auto result = get_matching_names(std::bind(prefix_match, _1, real_prefix));
    if (result.empty())
        result = get_matching_names(std::bind(subsequence_match, _1, real_prefix));
    return result;
}

OptionManager::OptionList OptionManager::flatten_options() const
{
    OptionList res = m_parent ? m_parent->flatten_options() : OptionList{};
    for (auto& option : m_options)
    {
        auto it = find_option(res, option->name());
        if (it != res.end())
            *it = option.get();
        else
            res.emplace_back(option.get());
    }
    return res;
}

void OptionManager::on_option_changed(const Option& option)
{
    // if parent option changed, but we overrided it, it's like nothing happened
    if (&option.manager() != this and
        find_option(m_options, option.name()) != m_options.end())
        return;

    for (auto watcher : m_watchers)
        watcher->on_option_changed(option);
}

}
