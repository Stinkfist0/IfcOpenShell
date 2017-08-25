/********************************************************************************
 *                                                                              *
 * This file is part of IfcOpenShell.                                           *
 *                                                                              *
 * IfcOpenShell is free software: you can redistribute it and/or modify         *
 * it under the terms of the Lesser GNU General Public License as published by  *
 * the Free Software Foundation, either version 3.0 of the License, or          *
 * (at your option) any later version.                                          *
 *                                                                              *
 * IfcOpenShell is distributed in the hope that it will be useful,              *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of               *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                 *
 * Lesser GNU General Public License for more details.                          *
 *                                                                              *
 * You should have received a copy of the Lesser GNU General Public License     *
 * along with this program. If not, see <http://www.gnu.org/licenses/>.         *
 *                                                                              *
 ********************************************************************************/

#ifndef FILTERS_H
#define FILTERS_H

#include "../ifcgeom/IfcGeomFilter.h"

#include <boost/program_options.hpp>

struct geom_filter
{
    geom_filter(bool include, bool traverse) : type(UNUSED), include(include), traverse(traverse) {}
    geom_filter() : type(UNUSED), include(false), traverse(false) {}
    enum filter_type { UNUSED, ENTITY_TYPE, LAYER_NAME, ENTITY_ARG };
    filter_type type;
    bool include;
    bool traverse;
    std::string arg;
    std::set<std::string> values;
};
// Specialized classes for knowing which type of filter we are validating within validate().
// Could not figure out easily how else to know it if using single type for both.
struct inclusion_filter : public geom_filter { inclusion_filter() : geom_filter(true, false) {} };
struct inclusion_traverse_filter : public geom_filter { inclusion_traverse_filter() : geom_filter(true, true) {} };
struct exclusion_filter : public geom_filter { exclusion_filter() : geom_filter(false, false) {} };
struct exclusion_traverse_filter : public geom_filter { exclusion_traverse_filter() : geom_filter(false, true) {} };

typedef std::map<std::string, IfcGeom::string_arg_filter> arg_filter_map_t;

void parse_filter(geom_filter &filter, const std::vector<std::string>& values)
{
    namespace po = boost::program_options;
    if (values.size() == 0) {
        throw po::validation_error(po::validation_error::at_least_one_value_required);
    }
    std::string type = *values.begin();
    if (type == "entities") {
        filter.type = geom_filter::ENTITY_TYPE;
    } else if (type == "layers") {
        filter.type = geom_filter::LAYER_NAME;
    } else if (type == "arg") {
        filter.type = geom_filter::ENTITY_ARG;
        filter.arg = *(values.begin() + 1);
    } else {
        throw po::validation_error(po::validation_error::invalid_option_value);
    }
    filter.values.insert(values.begin() + (filter.type == geom_filter::ENTITY_ARG ? 2 : 1), values.end());
}

bool append_filter(const std::string& type, const std::vector<std::string>& values, geom_filter& filter)
{
    geom_filter temp;
    parse_filter(temp, values);
    // Merge values only if type and arg match.
    if ((filter.type != geom_filter::UNUSED && filter.type != temp.type) || (!filter.arg.empty() && filter.arg != temp.arg)) {
        std::cerr << "[Error] Multiple '" << type << "' filters specified with different criteria\n";
        return false;
    }
    filter.type = temp.type;
    filter.values.insert(temp.values.begin(), temp.values.end());
    filter.arg = temp.arg;
    return true;
}

size_t read_filters_from_file(
    const std::string& filename,
    inclusion_filter& include_filter,
    inclusion_traverse_filter& include_traverse_filter,
    exclusion_filter& exclude_filter,
    exclusion_traverse_filter& exclude_traverse_filter)
{
    std::ifstream filter_file(filename.c_str());
    if (!filter_file.is_open()) {
        std::cerr << "[Error] Unable to open filter file '" + filename + "' or the file does not exist.\n";
        return 0;
    }

    size_t line_number = 1, num_filters = 0;
    for (std::string line; std::getline(filter_file, line); ++line_number) {
        boost::trim(line);
        if (line.empty()) {
            continue;
        }

        std::vector<std::string> values;
        boost::split(values, line, boost::is_any_of("\t "), boost::token_compress_on);
        if (values.empty()) {
            continue;
        }

        std::string type = values.front();
        values.erase(values.begin());
        // Support both "--include=arg GlobalId 1VQ5n5$RrEbPk8le4ZCI81" and "include arg GlobalId 1VQ5n5$RrEbPk8le4ZCI81"
        // and tolerate extraneous whitespace.
        boost::trim_left_if(type, boost::is_any_of("-"));
        size_t equal_pos = type.find('=');
        if (equal_pos != std::string::npos) {
            std::string value = type.substr(equal_pos + 1);
            type = type.substr(0, equal_pos);
            values.insert(values.begin(), value);
        }

        try {
            if (type == "include") { if (append_filter("include", values, include_filter)) { ++num_filters; } }
            else if (type == "include+") { if (append_filter("include+", values, include_traverse_filter)) { ++num_filters; } }
            else if (type == "exclude") { if (append_filter("exclude", values, exclude_filter)) { ++num_filters; } }
            else if (type == "exclude+") { if (append_filter("exclude+", values, exclude_traverse_filter)) { ++num_filters; } }
            else {
                std::cerr << "[Error] Invalid filtering type at line " + boost::lexical_cast<std::string>(line_number) + "\n";
                return 0;
            }
        } catch(...) {
            std::cerr << "[Error] Unable to parse filter at line " + boost::lexical_cast<std::string>(line_number) + ".\n";
            return 0;
        }
    }
    return num_filters;
}

/// @todo Clean up this filter initialization code further.
/// @return References to the used filter functors, if none an error occurred.
std::vector<IfcGeom::filter_t> setup_filters(
    const std::vector<geom_filter>& filters,
    const std::string& output_extension,
    IfcGeom::entity_filter& entity_filter,
    IfcGeom::layer_filter& layer_filter,
    arg_filter_map_t& arg_filters
)
{
    std::vector<IfcGeom::filter_t> filter_funcs;
    foreach(const geom_filter& f, filters) {
        if (f.type == geom_filter::ENTITY_TYPE) {
            entity_filter.include = f.include;
            entity_filter.traverse = f.traverse;
            try {
                entity_filter.populate(f.values);
            } catch (const IfcParse::IfcException& e) {
                std::cerr << "[Error] " << e.what() << std::endl;
                return std::vector<IfcGeom::filter_t>();
            }
        } else if (f.type == geom_filter::LAYER_NAME) {
            layer_filter.include = f.include;
            layer_filter.traverse = f.traverse;
            layer_filter.populate(f.values);
        } else if (f.type == geom_filter::ENTITY_ARG) {
            try {
                IfcGeom::string_arg_filter arg_filter(f.arg);
                arg_filter.include = f.include;
                arg_filter.traverse = f.traverse;
                arg_filter.populate(f.values);
                std::pair<arg_filter_map_t::iterator, bool> inserted = arg_filters.insert(std::make_pair(f.arg, arg_filter));
                if (inserted.second) {
                    // new arg filter entry
                    filter_funcs.push_back(boost::ref(inserted.first->second));
                }
            } catch (const IfcParse::IfcException& e) {
                std::cerr << "[Error] " << e.what() << std::endl;
                return std::vector<IfcGeom::filter_t>();
            }
        }
    }

    // If no entity names are specified these are the defaults to skip from output
    if (entity_filter.values.empty()) {
        try {
            std::set<std::string> entities;
            entities.insert("IfcSpace");
            if (output_extension == ".svg") {
                entity_filter.include = true;
            } else {
                entities.insert("IfcOpeningElement");
            }
            entity_filter.populate(entities);
        } catch (const IfcParse::IfcException& e) {
            std::cerr << "[Error] " << e.what() << std::endl;
            return std::vector<IfcGeom::filter_t>();
        }
    }

    if (!layer_filter.values.empty()) {
        filter_funcs.push_back(boost::ref(layer_filter));
    }
    if (!entity_filter.values.empty()) {
        filter_funcs.push_back(boost::ref(entity_filter));
    }

    return filter_funcs;
}

#endif
