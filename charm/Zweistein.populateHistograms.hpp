/***************************************************************************
 *   Copyright (C) 2019-2020  by Andreas Langhoff						   *
 *   <andreas.langhoff@frm2.tum.de>									       *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation;                                         *
 ***************************************************************************/

#pragma once
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/chrono.hpp>
#include "Zweistein.Event.hpp"
#include "Mesytec.Mcpd8.hpp"
#include <boost/format.hpp>
#include <boost/signals2.hpp>
#include <boost/thread.hpp>
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <boost/asio.hpp>
#include "Zweistein.Histogram.hpp"
#include "Zweistein.Binning.hpp"
#include "Zweistein.Logger.hpp"
#include <boost/filesystem.hpp>
#include <cctype>
#include <sstream>
#include "Zweistein.GetConfigDir.hpp"
#include <magic_enum.hpp>

namespace Zweistein {
	
	

	void setupHistograms(boost::asio::io_service& io_service, boost::shared_ptr < Mesytec::MesytecSystem> pmsmtsystem1, std::string binningfile1) {
		using namespace magic_enum::bitwise_operators; // out-of-the-box bitwise operators for enums.

		setup_status = histogram_setup_status::not_done;

		unsigned short maxX = pmsmtsystem1->data.widthX;
		unsigned short maxY = pmsmtsystem1->data.widthY;

		if (maxX == 0 || maxY == 0) {
			LOG_ERROR << __FILE__ << " : " << __LINE__ << " maxX=" << maxX << ", maxY=" << maxY << std::endl;
			return;
		}
	
		//LOG_DEBUG << "pmsmtsystem1->data.widthX=" << maxX << ", pmsmtsystem1->data.widthY=" << maxY << std::endl;
		int left = 0;
		int bottom = 0;
		{
			Zweistein::WriteLock w_lock(histogramsLock);
			histograms[0].resize(maxY, maxX);
			std::string wkt = histograms[0].WKTRoiRect(left, bottom, maxX, maxY);
			histograms[0]._setRoi(wkt, 0);
		}
		histogram_setup_status hss = setup_status;
		setup_status = hss | histogram_setup_status::histogram0_resized;

		if (!binningfile1.empty() || binningfile1.length() != 0) {
			try {
				
				boost::filesystem::path p(binningfile1);
				std::string ext = boost::algorithm::to_lower_copy(p.extension().string());
				if (ext == ".txt") {
					Zweistein::Binning::ReadTxt(binningfile1);
				}
				else if (ext == ".json") {
					Zweistein::Binning::Read(binningfile1);
				}

				else {
					LOG_ERROR << "Binning not handled:" << binningfile1 << std::endl;
				}


				auto s = Zweistein::Binning::BINNING.shape();

				if (s[0] > maxX) {
					LOG_ERROR << "BINNING.shape()[0]=" << s[0] << " greater than detector sizeX(" << maxX << ")" << std::endl;
					io_service.stop();

				}

				if (s[1] > maxY) {
					LOG_ERROR << "BINNING.shape()[1]=" << s[1] << " greater than detector sizeY(" << maxY << ")" << std::endl;
					io_service.stop();

				}

				if (s[1] < maxY) {
					LOG_WARNING << "BINNING.shape()[1]=" << s[1] << " smaller than detector sizeY(" << maxY << "), detector partially unused." << std::endl;
				}
				hss = setup_status;
				setup_status = hss | histogram_setup_status::has_binning;
			}
			catch (std::exception& e) {
				LOG_WARNING << e.what() << " for reading." << std::endl;
				int newy = maxY / 8;
				Zweistein::Binning::GenerateSimple(newy, maxY, maxX);
				std::stringstream ss;
				ss << std::endl << "Zweistein::Binning::GenerateSimple(" << newy << "," << maxY << "," << maxX << ") ";

				try {
					boost::filesystem::path p(binningfile1);
					std::string f = "y" + std::to_string(newy) + "-" + std::to_string(maxY) + "_x" + std::to_string(maxX) + "_binning.json";
					p = p.remove_filename();
					std::string file = p.append(f).string();
					Zweistein::Binning::Write(file);
					ss << "=> " << file << " " << std::endl;
					LOG_WARNING << ss.str();
					histogram_setup_status hss = setup_status;
					setup_status = hss | histogram_setup_status::has_binning;

				}
				catch (std::exception& e) {
					ss << e.what() << " for writing." << std::endl;
					LOG_ERROR << ss.str();

				}

			}
			
			auto s = Zweistein::Binning::BINNING.shape();

			Zweistein::Binning::array_type::element* itr = Zweistein::Binning::BINNING.data();

			short max_value = 0;
			for (int i = 0; i < s[0] * s[1]; i++) {
				if (*itr > max_value) max_value = *itr;
				itr++;
			}
			int power = 2;
			while (power < max_value) power *= 2;

			{
				
				Zweistein::WriteLock w_lock(histogramsLock);
				histograms[1].resize(power, maxX);
				std::string wkt = histograms[1].WKTRoiRect(0, 0, maxX, power);
				histograms[1]._setRoi(wkt, 0);
				LOG_INFO << "histograms[1] :rows=" << histograms[1].histogram.rows << ", cols=" << histograms[1].histogram.cols << std::endl;
				LOG_INFO << "Zweistein::Binning::BINNING.shape(" << s[0] << "," << s[1] << ")" << std::endl;
			}

			hss = setup_status;
			setup_status = hss | histogram_setup_status::histogram1_resized ;
			
			
		}

		hss = setup_status;
		setup_status = hss | histogram_setup_status::done;
	}


	void populateHistograms(boost::asio::io_service & io_service, boost::shared_ptr < Mesytec::MesytecSystem> pmsmtsystem1) {
		
		using namespace magic_enum::bitwise_operators; // out-of-the-box bitwise operators for enums.
		boost::chrono::system_clock::time_point start = boost::chrono::system_clock::now();
		try {
			Zweistein::Event ev;
			int i = 0;
			long long nloop = 0;
			do {
				boost::chrono::system_clock::time_point current = boost::chrono::system_clock::now();
				histogram_setup_status hss = setup_status;
				if (!magic_enum::enum_integer(hss & histogram_setup_status::done)) {
					boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
					continue;
				}
				unsigned short maxX = pmsmtsystem1->data.widthX;
				unsigned short maxY = pmsmtsystem1->data.widthY;

				unsigned short binningMaxY = 0;
				if (magic_enum::enum_integer(hss & histogram_setup_status::has_binning)) {
					binningMaxY = (unsigned short) Zweistein::Binning::BINNING.shape()[1];
				}
				
				{
					
					Zweistein::WriteLock w_lock(histogramsLock);
					long evntspopped = 0;
					while (pmsmtsystem1->data.evntqueue.pop(ev)) {
					evntspopped++;
					if (evntspopped > 2 * Mcpd8::Data::EVENTQUEUESIZE / 3) {
						evntspopped = 0;
						LOG_WARNING << "evntspopped > " << 2 * Mcpd8::Data::EVENTQUEUESIZE / 3 << std::endl;
						break;
					}

					if (evntspopped > 1000) { // to save cpu time in this critical loop
						// we have to give free histogramsGuard once a while (at least every 200 milliseconds)
						// so that other code can change Roi data.
						auto  elapsed = boost::chrono::duration_cast<boost::chrono::milliseconds>(boost::chrono::system_clock::now() - current);
						if (elapsed.count() > 200) {
							current = boost::chrono::system_clock::now();
							break;
						}
					}



					if (ev.type == Zweistein::Event::EventTypeOther::RESET) {
						for (auto& h : histograms) {
							for (auto& r : h.roidata) 	r.count = 0;
							h.resize(h.histogram.size[0], h.histogram.size[1]);
						}
						continue;
					}
					if (ev.X < 0 || ev.X >= maxX) {
						LOG_ERROR << "0 < Event.X=" << ev.X << " < " << maxX << " Event.X ouside bounds" << std::endl;
						io_service.stop();
						break;
					}
					if (ev.Y < 0 || ev.Y >= maxY) {
						LOG_ERROR << "0< Event.Y=" << ev.Y << " < " << maxY << " Event.Y ouside bounds" << std::endl;
						io_service.stop();
						break;

					}

					// this is our raw histograms[0]
					point_type p(ev.X, ev.Y);
					histograms[0].histogram.at<int32_t>(p.y(), p.x()) += 1;

					if (boost::geometry::covered_by(p, histograms[0].roidata[0].roi)) {
						auto size = histograms[0].histogram.size;
						histograms[0].roidata[0].count += 1;
					}
					if (magic_enum::enum_integer(hss & histogram_setup_status::has_binning)) {
						// this is our binned histograms[1]
						if (ev.Y >= binningMaxY) continue; // skip it
						short binnedY = Zweistein::Binning::BINNING[ev.X][ev.Y];

						if (binnedY < 0) continue; // skip also

						point_type pb(ev.X, binnedY);
						for (auto& r : histograms[1].roidata) {
							histograms[1].histogram.at<int32_t>(pb.y(), pb.x()) += 1;
							if (boost::geometry::covered_by(pb, r.roi)) {
								r.count += 1;
							}
						}

					}
				}
					nloop++;
				}
			} while (!io_service.stopped());
		}
		catch (boost::exception & e) {
				LOG_ERROR<<boost::diagnostic_information(e ) << std::endl;
		}
		LOG_DEBUG << "populateHistograms() exiting..." << std::endl;
	}


}