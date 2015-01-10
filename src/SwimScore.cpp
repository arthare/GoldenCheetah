/*
 * Copyright (c) 2008 Sean C. Rhea (srhea@srhea.net)
 *               2010 Mark Liversedge (liversedge@gmail.com)
 *               2014 Alejandro Martinez (amtriathlon@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "RideMetric.h"
#include "PaceZones.h"
#include "Units.h"
#include "RideItem.h"
#include <cmath>
#include <algorithm>
#include <QApplication>

// NOTE: This code follows the description of SwimScore in 
// "Calculating Power Output and Training Stress in Swimmers:
// The Development of the SwimScoreTM Algorithm", by Dr. Phil Skiba:
// http://www.physfarm.com/swimscore.pdf

// Swimming Power from Speed
static inline double swimming_power( double weight, double speed ) {
    const double K = 0.35 * weight + 2; // Drag Factor (Eq. 6)
    const double ep = 0.6; // Toussaintís propelling efficiency

    return (K / ep) * pow(speed, 3); // Eq. 5
}

// Swimming Speed from Power
static inline double swimming_speed( double weight, double power ) {
    const double K = 0.35 * weight + 2; // Drag Factor (Eq. 6)
    const double ep = 0.6; // Toussaintís propelling efficiency

    return pow((ep / K) * power, 1/3.0); // Eq. 5
}

// XPowerSwim, used for SwimScore and xPaceSwim calculation
class XPowerSwim : public RideMetric {
    Q_DECLARE_TR_FUNCTIONS(XPowerSwim)
    double xpower;
    double secs;

    public:

    XPowerSwim() : xpower(0.0), secs(0.0)
    {
        setSymbol("swimscore_xpower");
        setInternalName("xPower Swim");
    }
    void initialize() {
        setName(tr("xPower Swim"));
        setType(RideMetric::Average);
        setMetricUnits(tr("watts"));
        setImperialUnits(tr("watts"));
    }

    void compute(const RideFile *ride, const Zones *, int,
                 const HrZones *, int,
                 const QHash<QString,RideMetric*> &,
                 const Context *) {

        // xPowerSwim only makes sense for running
        if (!ride->isSwim()) return;

        // unconst naughty boy, get athlete's data
        RideFile *uride = const_cast<RideFile*>(ride);
        double weight = uride->getWeight();

        static const double EPSILON = 0.1;
        static const double NEGLIGIBLE = 0.1;

        double secsDelta = ride->recIntSecs();
        double sampsPerWindow = 25.0 / secsDelta;
        double attenuation = sampsPerWindow / (sampsPerWindow + secsDelta);
        double sampleWeight = secsDelta / (sampsPerWindow + secsDelta);

        double lastSecs = 0.0;
        double weighted = 0.0;

        double total = 0.0;
        int count = 0;

        foreach(const RideFilePoint *point, ride->dataPoints()) {
            while ((weighted > NEGLIGIBLE)
                   && (point->secs > lastSecs + secsDelta + EPSILON)) {
                weighted *= attenuation;
                lastSecs += secsDelta;
                total += pow(weighted, 3.0);
                count++;
            }
            weighted *= attenuation;
            weighted += sampleWeight * swimming_power(weight, point->kph/3.6);
            lastSecs = point->secs;
            total += pow(weighted, 3.0);
            count++;
        }
        xpower = pow(total / count, 1/3.0);
        secs = count * secsDelta;

        setValue(xpower);
        setCount(secs);
    }
    bool isRelevantForRide(const RideItem*ride) const { return ride->isSwim; }
    RideMetric *clone() const { return new XPowerSwim(*this); }
};

// xPaceSwim: constant Pace which requires the same xPowerSwim
class XPaceSwim : public RideMetric {
    Q_DECLARE_TR_FUNCTIONS(XPaceSwim)
    double xPaceSwim;

    public:

    XPaceSwim() : xPaceSwim(0.0)
    {
        setSymbol("swimscore_xpace");
        setInternalName("xPace Swim");
    }
    void initialize() {
        setName(tr("xPace Swim"));
        setType(RideMetric::Average);
        setMetricUnits(tr("min/100m"));
        setImperialUnits(tr("min/100yd"));
        setPrecision(1);
        setConversion(METERS_PER_YARD);
    }
    void compute(const RideFile *ride, const Zones *, int,
                 const HrZones *, int,
                 const QHash<QString,RideMetric*> &deps,
                 const Context *) {
        // xPaceSwim only makes sense for swimming
        if (!ride->isSwim()) return;

        // unconst naughty boy, get athlete's data
        RideFile *uride = const_cast<RideFile*>(ride);
        double weight = uride->getWeight();

        assert(deps.contains("swimscore_xpower"));
        XPowerSwim *xPowerSwim = dynamic_cast<XPowerSwim*>(deps.value("swimscore_xpower"));
        assert(xPowerSwim);
        double watts = xPowerSwim->value(true);

        double speed = swimming_speed(weight, watts);
        
        xPaceSwim = speed ? (100.0/60.0) / speed : 0.0;

        setValue(xPaceSwim);
    }
    bool isRelevantForRide(const RideItem *ride) const { return ride->isSwim; }
    RideMetric *clone() const { return new XPaceSwim(*this); }
};

// Swimming Threshold Power based on CV, used for SwimScore calculation
class STP : public RideMetric {
    Q_DECLARE_TR_FUNCTIONS(STP)

    public:

    STP()
    {
        setSymbol("swimscore_tp");
        setInternalName("STP");
    }
    void initialize() {
        setName(tr("STP"));
        setType(RideMetric::Average);
        setMetricUnits(tr("watts"));
        setImperialUnits(tr("watts"));
        setPrecision(0);
    }
    void compute(const RideFile *ride, const Zones *, int ,
                 const HrZones *, int,
                 const QHash<QString,RideMetric*> &,
                 const Context *context) {
        // STP only makes sense for running
        if (!ride->isSwim()) return;

        // unconst naughty boy, get athlete's data
        RideFile *uride = const_cast<RideFile*>(ride);
        double weight = uride->getWeight();
        
        const PaceZones *zones = context->athlete->paceZones(true);
        int zoneRange = context->athlete->paceZones(true)->whichRange(ride->startTime().date());

        // did user override for this ride?
        double cv = ride->getTag("CV","0").toInt();

        // not overriden so use the set value
        // if it has been set at all
        if (!cv && zones && zoneRange >= 0) 
            cv = zones->getCV(zoneRange);
        
        // Swimming power at cv
        double watts = swimming_power(weight, cv/3.6);

        setValue(watts);
    }
    bool isRelevantForRide(const RideItem *ride) const { return ride->isSwim; }
    RideMetric *clone() const { return new STP(*this); }
};

// Swimming Relative Intensity
class SRI : public RideMetric {
    Q_DECLARE_TR_FUNCTIONS(SRI)
    double reli;
    double secs;

    public:

    SRI() : reli(0.0), secs(0.0)
    {
        setSymbol("swimscore_ri");
        setInternalName("SRI");
    }
    void initialize() {
        setName(tr("SRI"));
        setType(RideMetric::Average);
        setMetricUnits(tr(""));
        setImperialUnits(tr(""));
        setPrecision(2);
    }
    void compute(const RideFile *ride, const Zones *, int,
                 const HrZones *, int,
                 const QHash<QString,RideMetric*> &deps,
                 const Context *) {
        // SRI only makes sense for swimming
        if (!ride->isSwim()) return;

        assert(deps.contains("swimscore_xpower"));
        XPowerSwim *xPowerSwim = dynamic_cast<XPowerSwim*>(deps.value("swimscore_xpower"));
        assert(xPowerSwim);
        assert(deps.contains("swimscore_tp"));
        STP *stp = dynamic_cast<STP*>(deps.value("swimscore_tp"));
        assert(stp);
        reli = stp->value(true) ? xPowerSwim->value(true) / stp->value(true) : 0;
        secs = xPowerSwim->count();

        setValue(reli);
        setCount(secs);
    }
    bool isRelevantForRide(const RideItem *ride) const { return ride->isSwim; }
    RideMetric *clone() const { return new SRI(*this); }
};

// SwimScore Metric for swimming
class SwimScore : public RideMetric {
    Q_DECLARE_TR_FUNCTIONS(SwimScore)
    double score;

    public:

    SwimScore() : score(0.0)
    {
        setSymbol("swimscore");
        setInternalName("SwimScore");
    }
    void initialize() {
        setName("SwimScore");
        setType(RideMetric::Total);
    }
    void compute(const RideFile *ride, const Zones *, int,
                 const HrZones *, int,
	    const QHash<QString,RideMetric*> &deps,
                 const Context *) {
        // SwimScore only makes sense for swimming
        if (!ride->isSwim()) return;

        assert(deps.contains("swimscore_xpower"));
        assert(deps.contains("swimscore_ri"));
        assert(deps.contains("swimscore_tp"));
        XPowerSwim *xPowerSwim = dynamic_cast<XPowerSwim*>(deps.value("swimscore_xpower"));
        assert(xPowerSwim);
        RideMetric *sri = deps.value("swimscore_ri");
        assert(sri);
        RideMetric *stp = deps.value("swimscore_tp");
        assert(stp);
        double normWork = xPowerSwim->value(true) * xPowerSwim->count();
        double rawGOVSS = normWork * sri->value(true);
        double workInAnHourAtSTP = stp->value(true) * 3600;
        score = workInAnHourAtSTP ? rawGOVSS / workInAnHourAtSTP * 100.0 : 0;

        setValue(score);
    }
    bool isRelevantForRide(const RideItem *ride) const { return ride->isSwim; }
    RideMetric *clone() const { return new SwimScore(*this); }
};

static bool addAllSwimScore() {
    RideMetricFactory::instance().addMetric(XPowerSwim());
    RideMetricFactory::instance().addMetric(STP());
    QVector<QString> deps;
    deps.append("swimscore_xpower");
    RideMetricFactory::instance().addMetric(XPaceSwim(), &deps);
    deps.append("swimscore_tp");
    RideMetricFactory::instance().addMetric(SRI(), &deps);
    deps.append("swimscore_ri");
    RideMetricFactory::instance().addMetric(SwimScore(), &deps);
    return true;
}

static bool SwimScoreAdded = addAllSwimScore();

// TriScore Metric for triathlon
class TriScore : public RideMetric {
    Q_DECLARE_TR_FUNCTIONS(TriScore)
    double score;

    public:

    TriScore() : score(0.0)
    {
        setSymbol("triscore");
        setInternalName("TriScore");
    }
    void initialize() {
        setName("TriScore");
        setType(RideMetric::Total);
    }
    void compute(const RideFile *ride, const Zones *, int,
                 const HrZones *, int,
	    const QHash<QString,RideMetric*> &deps,
                 const Context *) {

        if (ride->isSwim()) {
            assert(deps.contains("swimscore"));
            score = deps.value("swimscore")->value(true);
        } else if (ride->isRun()) {
            assert(deps.contains("govss"));
            score = deps.value("govss")->value(true);
        } else {
            assert(deps.contains("skiba_bike_score"));
            score = deps.value("skiba_bike_score")->value(true);
        }

        setValue(score);
    }
    RideMetric *clone() const { return new TriScore(*this); }
};

static bool addTriScore() {
    QVector<QString> deps;
    deps.append("swimscore");
    deps.append("govss");
    deps.append("skiba_bike_score");
    RideMetricFactory::instance().addMetric(TriScore(), &deps);
    return true;
}

static bool TriScoreAdded = addTriScore();
