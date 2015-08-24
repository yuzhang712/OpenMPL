/*************************************************************************
    > File Name: io.h
    > Author: Yibo Lin
    > Mail: yibolin@utexas.edu
    > Created Time: Thu 06 Nov 2014 08:53:46 AM CST
 ************************************************************************/

#ifndef SIMPLEMPL_IO_H
#define SIMPLEMPL_IO_H

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <limits>
#include <map>
#include <algorithm>
#include <numeric>
#include <boost/lexical_cast.hpp>
#include <limbo/parsers/gdsii/stream/GdsReader.h>
#include <limbo/parsers/gdsii/stream/GdsWriter.h>
#include <limbo/programoptions/ProgramOptions.h>

#include "db.h"

SIMPLEMPL_BEGIN_NAMESPACE

using std::cout;
using std::endl;
using std::vector;
using std::string;
using std::ifstream;
using std::ofstream;
using std::numeric_limits;
using std::map;
using std::pair;
using std::make_pair;

namespace gtl = boost::polygon;
using boost::int32_t;
using boost::int64_t;
using boost::array;
using gtl::point_concept;
using gtl::rectangle_concept;
using gtl::polygon_90_concept;
using gtl::polygon_90_set_concept;
using gtl::point_data;
using gtl::rectangle_data;
using gtl::polygon_90_data;
using gtl::polygon_90_set_data;

using namespace gtl::operators;

/// read gds file 
template <typename T>
struct GdsReader : GdsParser::GdsDataBase
{
	typedef T coordinate_type;
	typedef LayoutDB<coordinate_type> layoutdb_type;
	typedef typename layoutdb_type::point_type              point_type;
	typedef typename layoutdb_type::rectangle_type          rectangle_type;
	typedef typename layoutdb_type::polygon_type            polygon_type;
	typedef typename layoutdb_type::polygon_pointer_type    polygon_pointer_type;
	typedef typename layoutdb_type::rectangle_pointer_type  rectangle_pointer_type;
	typedef typename layoutdb_type::path_type               path_type;

	//string strname; // TOPCELL name, useful for dump out gds files 
	//double unit;
	int32_t layer;
	int32_t status; // 0: not in any block, 1 in BOUNDARY or BOX block, 2 in PATH   
	vector<point_type> vPoint;
	int64_t file_size; // in bytes 

	layoutdb_type& db;  

	GdsReader(layoutdb_type& _db) : db(_db) {}

	bool operator() (string const& filename)  
	{
		// calculate file size 
		ifstream in (filename.c_str());
		if (!in.good()) return false;
		std::streampos begin = in.tellg();
		in.seekg(0, std::ios::end);
		std::streampos end = in.tellg();
		file_size = (end-begin);
		in.close();
		// read gds 
		return GdsParser::read(*this, filename);
	}

	template <typename ContainerType>
	void general_cbk(string const& ascii_record_type, string const& ascii_data_type, ContainerType const& vData)
	{
		if (ascii_record_type == "UNITS")
		{
			db.unit = vData[1]; 
		}
		else if (ascii_record_type == "BOUNDARY" || ascii_record_type == "BOX")
		{
			vPoint.clear();
			layer = 0;
			status = 1;
		}
		else if (ascii_record_type == "PATH")
		{
			vPoint.clear();
			layer = 0;
			status = 2;
		}
		else if (ascii_record_type == "LAYER")
		{
			layer = vData[0];
		}
		else if (ascii_record_type == "XY")
		{
			if (status == 1 || status == 2)
			{
				mplAssert((vData.size() & 1) == 0 && vData.size() >= 4);
				vPoint.clear();
				uint32_t end = vData.size();
				// skip last point for BOX and BOUNDARY
				if (status == 1) end -= 2;
				for (uint32_t i = 0; i < end; i += 2)
					vPoint.push_back(gtl::construct<point_type>(vData[i], vData[i+1]));
			}
		}
		else if (ascii_record_type == "ENDEL")
		{
			if (status == 1 || status == 2)
			{
				mplAssert(layer != -1);

				db.add(layer, vPoint);

				status = 0;
			}
		}
		else if (ascii_record_type == "STRNAME")
		{
			mplAssert(ascii_data_type == "STRING");
			mplAssert(!vData.empty());
			db.strname.assign(vData.begin(), vData.end());
		}
	}

	// required callbacks in parser 
	virtual void bit_array_cbk(const char* ascii_record_type, const char* ascii_data_type, vector<int> const& vBitArray)
	{this->general_cbk(ascii_record_type, ascii_data_type, vBitArray);}
	virtual void integer_2_cbk(const char* ascii_record_type, const char* ascii_data_type, vector<int> const& vInteger)
	{this->general_cbk(ascii_record_type, ascii_data_type, vInteger);}
	virtual void integer_4_cbk(const char* ascii_record_type, const char* ascii_data_type, vector<int> const& vInteger)
	{this->general_cbk(ascii_record_type, ascii_data_type, vInteger);}
	virtual void real_4_cbk(const char* ascii_record_type, const char* ascii_data_type, vector<double> const& vFloat) 
	{this->general_cbk(ascii_record_type, ascii_data_type, vFloat);}
	virtual void real_8_cbk(const char* ascii_record_type, const char* ascii_data_type, vector<double> const& vFloat) 
	{this->general_cbk(ascii_record_type, ascii_data_type, vFloat);}
	virtual void string_cbk(const char* ascii_record_type, const char* ascii_data_type, string const& str) 
	{this->general_cbk(ascii_record_type, ascii_data_type, str);}
	virtual void begin_end_cbk(const char* ascii_record_type)
	{this->general_cbk(ascii_record_type, "", vector<int>());}

};

/// write gds file 
template <typename T>
struct GdsWriter
{
	typedef T coordinate_type;
	typedef LayoutDB<coordinate_type> layoutdb_type;
	typedef typename layoutdb_type::point_type point_type;
	typedef typename layoutdb_type::rectangle_type rectangle_type;
	typedef typename layoutdb_type::polygon_type polygon_type;
	typedef typename layoutdb_type::polygon_pointer_type polygon_pointer_type;
	typedef typename layoutdb_type::rectangle_pointer_type rectangle_pointer_type;
	typedef typename layoutdb_type::path_type path_type;

	void operator() (string const& filename, layoutdb_type const& db, 
			vector<pair<uint32_t, uint32_t> > const& vConflict, 
			vector<vector<uint32_t> > const& mAdjVertex, 
			string const& strname = "TOPCELL", double unit = 0.001) const 
	{
		GdsParser::GdsWriter gw (filename.c_str());
		gw.gds_create_lib("POLYGONS", unit /* um per bit */ );
		gw.gds_write_bgnstr();
		gw.gds_write_strname(strname.c_str());

		// if there are precolored patterns, keep the same layer convention 
		int32_t layer_offset = (db.sPrecolorLayer.empty())? 100 : *db.sPrecolorLayer.begin();
		// basic operation
		// will add more 
		(*this)(gw, db.vPattern, layer_offset);
		(*this)(gw, db.vPattern, vConflict,  layer_offset+db.color_num);   // conflict layer 
		(*this)(gw, db.vPattern, mAdjVertex, layer_offset+db.color_num+1); // draw edges 
		//(*this)(gw, db.hPath); // draw edges if there exits 

		gw.gds_write_endstr();
		gw.gds_write_endlib(); 
	}
	void operator() (GdsParser::GdsWriter& gw, vector<rectangle_pointer_type> const& vRect, const int32_t layer_offset) const 
	{
		for (typename vector<rectangle_pointer_type>::const_iterator it = vRect.begin(); it != vRect.end(); ++it)
		{
			rectangle_type const& rect = **it;
			gw.write_box(layer_offset+rect.color(), 0, 
					gtl::xl(rect), gtl::yl(rect), 
					gtl::xh(rect), gtl::yh(rect));
		}
	}
	void operator() (GdsParser::GdsWriter& gw, vector<rectangle_pointer_type> const& vRect, 
			vector<pair<uint32_t, uint32_t> > const& vConflict, const int32_t layer) const
	{
		for (vector<pair<uint32_t, uint32_t> >::const_iterator it = vConflict.begin(); it != vConflict.end(); ++it)
		{
			rectangle_type const& rect1 = *(vRect[it->first]);
			rectangle_type const& rect2 = *(vRect[it->second]);
			gw.write_box(layer, 0, 
					gtl::xl(rect1), gtl::yl(rect1), 
					gtl::xh(rect1), gtl::yh(rect1));
			gw.write_box(layer, 0, 
					gtl::xl(rect2), gtl::yl(rect2), 
					gtl::xh(rect2), gtl::yh(rect2));
		}
	}
	void operator() (GdsParser::GdsWriter& gw, map<int32_t, vector<path_type> > const& hPath) const 
	{
		for (typename map<int32_t, vector<path_type> >::const_iterator it1 = hPath.begin(); it1 != hPath.end(); ++it1)
		{
			const int32_t layer = it1->first;
			vector<path_type> const& vPath = it1->second;
			for (typename vector<path_type>::const_iterator it2 = vPath.begin(); it2 != vPath.end(); ++it2)
			{
				path_type const& path = *it2;
				// create a path
				gw.gds_write_path();
				gw.gds_write_layer(layer);
				gw.gds_write_datatype(0);
				gw.gds_write_pathtype(2); // extended square ends
				gw.gds_write_width(5); // 5 nm wide

				int32_t x[2] = {gtl::x(gtl::low(path)), gtl::x(gtl::high(path))};
				int32_t y[2] = {gtl::y(gtl::low(path)), gtl::y(gtl::high(path))};

				gw.gds_write_xy(x, y, 2);
				gw.gds_write_endel();
			}
		}
	}
	void operator() (GdsParser::GdsWriter& gw, vector<rectangle_pointer_type> const& vRect, vector<vector<uint32_t> > const& mAdjVertex, const int32_t layer) const 
	{
		for (uint32_t i = 0; i != mAdjVertex.size(); ++i)
		{
			for (uint32_t j = 0; j != mAdjVertex[i].size(); ++j)
			{
				uint32_t v = i;
				uint32_t u = mAdjVertex[i][j];
				// create a path from v to u 
				if (v < u) // avoid duplicate 
				{
					// create a path
					gw.gds_write_path();
					gw.gds_write_layer(layer);
					gw.gds_write_datatype(0);
					gw.gds_write_pathtype(2); // extended square ends
					gw.gds_write_width(5); // 5 nm wide

					point_type vc;
					point_type uc;
					gtl::center(vc, *vRect[v]);
					gtl::center(uc, *vRect[u]);
					int32_t x[2] = {gtl::x(vc), gtl::x(uc)};
					int32_t y[2] = {gtl::y(vc), gtl::y(uc)};

					gw.gds_write_xy(x, y, 2);
					gw.gds_write_endel();
				}
			}
		}
	}
};


/// parse command line arguments 
template <typename T>
struct CmdParser
{
	typedef T coordinate_type;
	typedef LayoutDB<coordinate_type> layoutdb_type;

	layoutdb_type& db;

	CmdParser(layoutdb_type& _db) : db(_db) {}

	bool operator()(int argc, char** argv)
	{
        bool help = false;
        string algo_str;
        // append options here 
        typedef limbo::programoptions::ProgramOptions po_type;
        using limbo::programoptions::Value;
        po_type desc (std::string("SimpleMPL 1.X Usage"));
        desc.add_option(Value<bool>("-help", &help, "print help message").toggle(true).default_value(false).toggle_value(true).help(true))
            .add_option(Value<string>("-in", &db.input_gds, "input gds file name").required(true))
            .add_option(Value<string>("-out", &db.output_gds, "output gds file name").default_value("output.gds"))
            .add_option(Value<double>("-coloring_distance", &db.coloring_distance_nm, "a floating point number indicating number of coloring distance in nanometer").default_value(0))
            .add_option(Value<int32_t>("-color_num", &db.color_num, "an integer indicating number of masks (colors)").required(true))
            .add_option(Value<int32_t>("-simplify_level", &db.simplify_level, "an integer indicating graph simplification level < 0|1|2 >").default_value(2))
            .add_option(Value<int32_t>("-thread_num", &db.thread_num, "an integer indicating maximum thread number").default_value(1))
            .add_option(Value<set<int32_t> >("-path_layer", &db.sPathLayer, "an integer indicating layer for conflict edges"))
            .add_option(Value<set<int32_t> >("-precolor_layer", &db.sPrecolorLayer, "an integer indicating layer for pre-colored patterns"))
            .add_option(Value<set<int32_t> >("-uncolor_layer", &db.sUncolorLayer, "an integer indicating layer for coloring").required(true))
            .add_option(Value<string>("-algo", &algo_str, "algorithm type < ILP|BACKTRACK >").default_value("BACKTRACK"))
            .add_option(Value<bool>("-verbose", &db.verbose, "control screen messages").toggle(true).default_value(false).toggle_value(true))
            ;
        try
        {
            desc.parse(argc, argv);

            // print help message 
            if (help)
            {
                std::cout << desc << "\n";
                exit(1);
            }

            // post processing algo_str 
            if (limbo::iequals(algo_str, "ILP")) 
            {
#if GUROBI == 1
                db.algo = AlgorithmTypeEnum::ILP_GURBOI;
#elif LEMONCBC == 1
                db.algo = AlgorithmTypeEnum::ILP_CBC;
#else 
                mplPrint(kWARN, "ILP is not available without GUROBI or CBC, set to default\n");
#endif
            }
            else if (limbo::iequals(algo_str, "LP"))
            {
#if GUROBI == 1
                db.algo = AlgorithmTypeEnum::LP_GUROBI;
#else 
                mplPrint(kWARN, "LP is not available without GUROBI, set to default\n");
#endif
            }
            else if (limbo::iequals(algo_str, "BACKTRACK"))
                db.algo = AlgorithmTypeEnum::BACKTRACK;
            else mplPrint(kWARN, "Unknown algorithm type %s, set to default\n", algo_str.c_str());

            // check condition 
            mplAssertMsg(db.coloring_distance_nm > 0 || !db.sPathLayer.empty(), "should set positive coloring_distance_nm or specify path_layer for conflict edges");
        }
        catch (std::exception& e)
        {
            // print help message and error message 
            std::cout << desc << "\n";
            mplPrint(kERROR, "%s\n", e.what());
            return false;
        }
		return true;
	}
};

SIMPLEMPL_END_NAMESPACE

#endif 
