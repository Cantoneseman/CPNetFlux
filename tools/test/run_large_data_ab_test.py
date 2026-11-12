#!/usr/bin/env python3
"""Test bounded large-file experiment matrix contracts without starting a transfer."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'perf'))
import run_large_data_ab as experiment


def m1_rows(candidate_times=None, misses=None):
    candidate_times = candidate_times or [2.15,2.12,2.17,2.14,2.16]
    misses = misses or [0]*10
    rows=[]
    baseline=[2.92,2.88,2.90,2.91,2.89]
    for rep in range(1,6):
        times={'default':baseline[rep-1],'combined':candidate_times[rep-1]}
        for arm in experiment.matrix_labels('m1',rep):
            seconds=times[arm]
            frame_bytes=65532 if arm=='default' else 262144
            label='default' if arm=='default' else 'combined'
            receive_buffer='off' if arm=='default' else '1048576'
            expected={'sha256':'same-tree','files':1,'bytes':8*experiment.base.GIB}
            rows.append({
                'arm':label,'rep':rep,'result':'pass','output_slot_removed':True,
                't_ready_seconds':seconds,'frame_bytes':frame_bytes,'receive_buffer':receive_buffer,
                'expected':expected,'actual_tree':dict(expected),
                'verified_chunks':8192,
                'resource':{'exit_code':0,'owned_swap_peak_bytes':0,'pswpout_delta':0,
                            'mem_available_min_bytes':16*experiment.base.GIB,
                            'cpu_seconds_sampled_lower_bound':1.0,'rss_peak_bytes':256*experiment.base.MiB},
                'source_residency':{'all_resident':True,'bytes':8*experiment.base.GIB,
                                    'process':{'swap_bytes':0},
                                    'host_pswpout_delta_since_seed':0},
                'mechanism':{'ranges':8,'actual_data_frames':8192*(17 if arm=='default' else 4)},
                'network_counter_deltas':{'rx_missed_errors':misses[(rep-1)*2+(arm=='combined')]},
            })
    return rows


def profile_overhead_rows():
    off_times=[.300,.310,.290,.300,.305]
    on_times=[.320,.330,.310,.320,.325]
    rows=[]
    for rep in range(1,6):
        times={'off':off_times[rep-1],'on':on_times[rep-1]}
        for arm in experiment.matrix_labels('profile-overhead',rep):
            expected={'sha256':'same-tree','files':1,'bytes':experiment.base.GIB}
            row={
                'arm':arm,'rep':rep,'result':'pass','output_slot_removed':True,
                't_ready_seconds':times[arm],'frame_bytes':262144,'receive_buffer':'off',
                'profile_enabled':arm=='on','range_timing_requested':True,
                'expected':expected,'actual_tree':dict(expected),
                'verified_chunks':1024,
                'resource':{'exit_code':0,'owned_swap_peak_bytes':0,'pswpout_delta':0,
                            'mem_available_min_bytes':16*experiment.base.GIB},
                'source_residency':{'all_resident':True,'bytes':experiment.base.GIB,
                                    'process':{'swap_bytes':0},
                                    'host_pswpout_delta_since_seed':0},
                'mechanism':{'ranges':8,'actual_data_frames':4096},
                'transfer_network_counter_deltas':{
                    'rx_missed_errors':0,'rx_out_of_buffer':0},
                'post_process_network_counter_deltas':{'rx_missed_errors':0},
                'receiver_network_window':{'before':{},'after_transfer':{},
                    'analysis':{'rx_missed_errors_delta':0,'rx_out_of_buffer_delta':0}},
            }
            if arm=='on':
                row['receiver_phases']={'receiver_profile':{
                    'payload':{'totals':{'data_frame_count':4096}},'cpu':{'totals':{}}}}
            else:
                row['receiver_phases']={'count':8}
            rows.append(row)
    return rows


class LargeFileExperimentTest(unittest.TestCase):
    def test_arm_order_is_interleaved_ab_ba(self):
        self.assertEqual(experiment.matrix_labels('m1',1),('default','combined'))
        self.assertEqual(experiment.matrix_labels('m1',2),('combined','default'))

    def test_arm_options_preserve_existing_matrices_and_define_m1(self):
        self.assertEqual(experiment.arm_options('frame','65532'),(65532,'off'))
        self.assertEqual(experiment.arm_options('receive-buffer','1048576'),(262144,'1048576'))
        self.assertEqual(experiment.arm_options('m1','default'),(None,'off'))
        self.assertEqual(experiment.arm_options('m1','combined'),(262144,'1048576'))

    def test_profile_overhead_matrix_keeps_the_transfer_configuration_fixed(self):
        self.assertEqual(experiment.matrix_labels('profile-overhead',1),('off','on'))
        self.assertEqual(experiment.matrix_labels('profile-overhead',2),('on','off'))
        self.assertEqual(experiment.arm_options('profile-overhead','off'),(262144,'off'))
        self.assertEqual(experiment.arm_options('profile-overhead','on'),(262144,'off'))
        experiment.validate_receiver_profile_request('matrix','profile-overhead',1,False)
        with self.assertRaisesRegex(ValueError,'profile-overhead'):
            experiment.validate_receiver_profile_request('matrix','profile-overhead',8,False)

    def test_mapping_work_counts_scale_from_one_to_eight_gib(self):
        one=experiment.base.GIB
        eight=8*one
        self.assertEqual(experiment.expected_write_calls(one,65532),2176)
        self.assertEqual(experiment.expected_write_calls(one,262144),512)
        self.assertEqual(experiment.expected_write_calls(eight,65532),17408)
        self.assertEqual(experiment.expected_write_calls(eight,262144),4096)
        self.assertEqual(eight//(512*experiment.base.MiB),16)

    def test_network_counter_regression_is_rejected(self):
        self.assertEqual(experiment.network_counter_deltas({'rx_missed_errors':5},{'rx_missed_errors':7}),
                         {'rx_missed_errors':2})
        with self.assertRaisesRegex(ValueError,'regressed'):
            experiment.network_counter_deltas({'rx_missed_errors':7},{'rx_missed_errors':5})

    def test_receiver_profile_is_limited_to_one_gib_diagnostic_matrices(self):
        experiment.validate_receiver_profile_request('matrix','frame',1,True)
        experiment.validate_receiver_profile_request('matrix','receive-buffer',1,True)
        for args in (('matrix','m1',8),('matrix','frame',8),('seed','frame',1)):
            with self.assertRaisesRegex(ValueError,'receiver profile'):
                experiment.validate_receiver_profile_request(*args,True)
        experiment.validate_receiver_profile_request('matrix','m1',8,False)

    def test_profile_rows_require_payload_and_cpu_observations_for_every_range(self):
        def profile_range():
            payload={'schema':'cpnetflux_receiver_payload_observation_v2','scope':'payload scope'}
            payload.update({key:0 for key in experiment.base.RECEIVER_PAYLOAD_PROFILE_FIELDS})
            cpu={'schema':'cpnetflux_receiver_cpu_observation_v1','scope':'cpu scope','write_cpu_owner':'writer'}
            cpu.update({key:0 for key in experiment.base.RECEIVER_CPU_PROFILE_FIELDS})
            return {'range_observation_enabled':True,'receiver_payload_observation':payload,
                    'receiver_cpu_observation':cpu}
        complete=[profile_range() for _ in range(8)]
        experiment.validate_profile_rows(complete,True)
        with self.assertRaisesRegex(AssertionError,'profile observation'):
            experiment.validate_profile_rows(complete[:-1]+[{'range_observation_enabled':True,
                'receiver_payload_observation':None,'receiver_cpu_observation':{'schema':'cpnetflux_receiver_cpu_observation_v1'}}],True)
        incomplete=profile_range()
        del incomplete['receiver_payload_observation']['logical_copy_bytes']
        with self.assertRaisesRegex(AssertionError,'profile fields missing'):
            experiment.validate_profile_rows([incomplete],True)
        profile={'payload':{'totals':{'data_frame_count':512}}}
        experiment.validate_profile_frame_count(profile,512)
        with self.assertRaisesRegex(AssertionError,'frame count mismatch'):
            experiment.validate_profile_frame_count(profile,511)
        with self.assertRaisesRegex(AssertionError,'range timing'):
            experiment.validate_profile_rows([{'range_observation_enabled':False}],True)
        no_profile=[{'range_observation_enabled':True,'receiver_payload_observation':None,
                     'receiver_cpu_observation':None}]
        experiment.validate_profile_rows(no_profile,False,range_timing=True)

    def test_profile_analysis_is_explicitly_non_performance_evidence(self):
        report=experiment.annotate_profile_analysis({'arms':{}},True)
        self.assertTrue(report['receiver_profile_enabled'])
        self.assertFalse(report['throughput_is_performance_evidence'])
        normal=experiment.annotate_profile_analysis({'arms':{}},False)
        self.assertNotIn('receiver_profile_enabled',normal)

    def test_profile_overhead_reports_paired_instrumentation_effect_only(self):
        result=experiment.evaluate_profile_overhead(profile_overhead_rows(),experiment.base.GIB)
        self.assertEqual(len(result['paired_slowdown_fractions']),5)
        self.assertGreaterEqual(result['median_paired_slowdown_fraction'],.05)
        self.assertTrue(result['repeatable_profile_perturbation'])
        self.assertFalse(result['throughput_is_performance_evidence'])

    def test_profile_overhead_rejects_transfer_window_receive_counter_events(self):
        rows=profile_overhead_rows()
        rows[0]['transfer_network_counter_deltas']['rx_out_of_buffer']=1
        with self.assertRaisesRegex(AssertionError,'profile overhead network gate'):
            experiment.evaluate_profile_overhead(rows,experiment.base.GIB)

    def test_m1_passes_only_with_rate_stability_pairs_and_network_gates(self):
        result=experiment.evaluate(m1_rows(),8*experiment.base.GIB)
        self.assertTrue(result['performance_gate_pass'])
        self.assertTrue(result['network_gate_pass'])
        self.assertTrue(result['m1_pass'])
        self.assertEqual(result['improved_pairs'],5)
        self.assertGreaterEqual(result['arms']['combined']['median_gbps'],30)

    def test_m1_rejects_incomplete_pairs(self):
        with self.assertRaisesRegex(AssertionError,'ten 8 GiB M1 tasks'):
            experiment.evaluate(m1_rows()[:-1],8*experiment.base.GIB)

    def test_m1_rejects_wrong_order_and_arm_configuration(self):
        rows=m1_rows()
        rows[0],rows[1]=rows[1],rows[0]
        with self.assertRaisesRegex(AssertionError,'AB/BA interleaving'):
            experiment.evaluate(rows,8*experiment.base.GIB)
        rows=m1_rows()
        rows[0]['receive_buffer']='1048576'
        with self.assertRaisesRegex(AssertionError,'receive_buffer'):
            experiment.evaluate(rows,8*experiment.base.GIB)

    def test_m1_keeps_network_failure_separate_from_performance(self):
        misses=[0]*10;misses[3]=1
        result=experiment.evaluate(m1_rows(misses=misses),8*experiment.base.GIB)
        self.assertTrue(result['performance_gate_pass'])
        self.assertFalse(result['network_gate_pass'])
        self.assertFalse(result['m1_pass'])


if __name__=='__main__':unittest.main()
